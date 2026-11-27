#define IMGUI_DEFINE_MATH_OPERATORS
#include "classes/planetnames.h"
#include <tuple>

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
#include "classes/alienorumid.h"
#include "classes/displayunits.h"
#include "universe_save_progress.h"
#include "inputs.h"
#include "loaders.h"
#include "housekeeping.h"
#include "imgui_internal.h"
#include "classes/clouds_gpu.h"
#include "classes/precipitationcache.h"
#include "classes/orbit_spacing.h"
#include "classes/neighborhoodfilter.h"
#include "classes/shore.h"
#include "classes/watercoverage.h"
#include "visuals.h"
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <unordered_map>

using namespace alienorum;

namespace
{
    std::unordered_map<int, WaterCoverageEditor> surface_editors;
    ObjectSearchResponse search_results;
    bool search_results_visible = false, search_list_focus = false;
    int search_result_row = -1;
}

void close_object_search_results()
{
    search_results_visible = search_list_focus = false;
    search_result_row = -1;
    search_results = {};
}

void open_object_search_results(ObjectSearchResponse results)
{
    search_results = std::move(results);
    search_result_row = -1;
    search_results_visible = true;
}

bool object_search_results_open() { return search_results_visible; }
bool object_search_list_focused() { return search_results_visible && search_list_focus; }

void draw_object_search_results(ImGuiIO& io)
{
    search_list_focus = false;
    if (!search_results_visible) return;
    {
        std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
        if (!resolve_object_search_match(search_results.anchor))
        {
            close_object_search_results();
            return;
        }
    }
    ImGui::SetNextWindowSize(ImVec2(563, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Search Results", &search_results_visible))
    {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) is_mouse_over_window = true;
        const float footer = 1.0 * ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginListBox("##object_search_list", ImVec2(-1, std::max(60.0f, ImGui::GetContentRegionAvail().y - footer))))
        {
            search_list_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            if (ImGui::BeginTable("##object_search_columns", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(search_results.matches.size()));
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        const auto& match = search_results.matches[row];
                        bool valid;
                        {
                            std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
                            valid = resolve_object_search_match(match) != nullptr;
                        }
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID(row);
                        ImGui::BeginDisabled(!valid);
                        if (ImGui::Selectable(match.name.c_str(), search_result_row == row,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnNav))
                            search_result_row = row;
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(match.type.c_str());
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndListBox();
        }
        const ObjectSearchMatch* target = search_result_row >= 0
            && search_result_row < static_cast<int>(search_results.matches.size()) ? &search_results.matches[search_result_row] : nullptr;
        bool valid = false;
        if (target)
        {
            std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
            valid = resolve_object_search_match(*target) != nullptr;
        }
        const std::pair<const char*, ObjectSearchAction> actions[] =
        {
            {"Select", ObjectSearchAction::select}, {"Focus", ObjectSearchAction::focus},
            {"Track", ObjectSearchAction::track}, {"Go", ObjectSearchAction::go},
            {"Alert on Rise", ObjectSearchAction::rise}, {"Alert on Set", ObjectSearchAction::set},
            {"Edit", ObjectSearchAction::edit}
        };
        bool first = true;
        for (const auto& action : actions)
        {
            const float width = ImGui::CalcTextSize(action.first).x + ImGui::GetStyle().FramePadding.x * 2;
            if (!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width
                <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
            ImGui::BeginDisabled(!valid || (action.second == ObjectSearchAction::edit && target && target->type == "object"));
            if (ImGui::Button(action.first) && target) apply_object_search_action(*target, action.second);
            ImGui::EndDisabled();
            first = false;
        }
    }

    ImVec2 cpos = ImGui::GetWindowPos(), csiz = ImGui::GetWindowSize();
    int dlgwidth = csiz.x, dlgheight = csiz.y, dlgtop = cpos.y, dlgleft = cpos.x;
    ImGui::End();
    if (io.MousePos.x >= dlgleft && io.MousePos.y >= dlgtop
        && io.MousePos.x < dlgleft+dlgwidth && io.MousePos.y < dlgtop+dlgheight)
        is_mouse_over_window = true;

    if (!search_results_visible) close_object_search_results();
}

void update_surface_edit_targets()
{
    for (auto it = surface_editors.begin(); it != surface_editors.end();)
    {
        it->second.poll(shore_displacement);
        if (!it->second.has_target() && (!objedtwnd || it->first != editidx)) it = surface_editors.erase(it);
        else ++it;
    }
}

// DST transition windows below are evaluated against local *standard* time
// (viewer_tz with no DST added), except EU/UK where the legal definition is
// anchored to UTC instead. Where a region's rule is phrased in daylight time
// (the US's November end date, AU/NZ's April end date) it is converted here
// to the equivalent standard-time instant, so every comparison is apples to
// apples. Ordinal Sundays are computed with Sakamoto's day-of-week algorithm
// rather than timegm()/mktime(), since the latter aren't portably available
// (MinGW lacks timegm) and behave inconsistently across platforms for
// pre-epoch or far-future years.
static int day_of_week(int y, int m, int d)            // 0 = Sunday.
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int nth_sunday(int year, int mon, int n)        // n=1 for the 1st Sunday, etc.
{
    return 1 + (7 - day_of_week(year, mon, 1)) % 7 + 7 * (n - 1);
}

static int last_sunday_of_month(int year, int mon)
{
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = days_in_month[mon - 1];
    if (mon == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dim = 29;
    return dim - day_of_week(year, mon, dim);
}

static bool time_before(int m1, int d1, int h1, int mi1, int s1, int m2, int d2, int h2, int mi2, int s2)
{
    if (m1 != m2) return m1 < m2;
    if (d1 != d2) return d1 < d2;
    if (h1 != h2) return h1 < h2;
    if (mi1 != mi2) return mi1 < mi2;
    return s1 < s2;
}

// US/Canada, Australia, and New Zealand: windows are defined against local
// standard time.
static bool in_local_standard_dst_window(DST_Rule rule, const struct tm &std_local_time)
{
    int year = std_local_time.tm_year + 1900, mon = std_local_time.tm_mon + 1, mday = std_local_time.tm_mday,
        hr = std_local_time.tm_hour, mn = std_local_time.tm_min, sc = std_local_time.tm_sec;

    switch (rule)
    {
        case dst_us_ca:
        {
            // Begins 02:00 standard time (2nd Sun Mar); ends 02:00 daylight
            // time (1st Sun Nov), i.e. 01:00 standard time.
            bool at_or_after_start = !time_before(mon, mday, hr, mn, sc, 3, nth_sunday(year, 3, 2), 2, 0, 0);
            bool before_end = time_before(mon, mday, hr, mn, sc, 11, nth_sunday(year, 11, 1), 1, 0, 0);
            return at_or_after_start && before_end;
        }

        case dst_au:
            // Southern hemisphere: the window spans the calendar year boundary.
            if (mon >= 10) return !time_before(mon, mday, hr, mn, sc, 10, nth_sunday(year, 10, 1), 2, 0, 0);
            if (mon < 4)   return true;
            if (mon == 4)  return time_before(mon, mday, hr, mn, sc, 4, nth_sunday(year, 4, 1), 2, 0, 0);
            return false;

        case dst_nz:
            if (mon > 9)   return true;
            if (mon == 9)  return !time_before(mon, mday, hr, mn, sc, 9, last_sunday_of_month(year, 9), 2, 0, 0);
            if (mon < 4)   return true;
            if (mon == 4)  return time_before(mon, mday, hr, mn, sc, 4, nth_sunday(year, 4, 1), 2, 0, 0);
            return false;

        default:
            return false;
    }
}

// EU/UK: window is defined against UTC, not local standard time - also used
// as a best-effort approximation for decree-based zones with no fixed
// formula (Beirut, Jerusalem, Cairo).
static bool in_eu_uk_dst_window(const struct tm &utc_time)
{
    int year = utc_time.tm_year + 1900, mon = utc_time.tm_mon + 1, mday = utc_time.tm_mday,
        hr = utc_time.tm_hour, mn = utc_time.tm_min, sc = utc_time.tm_sec;

    bool at_or_after_start = !time_before(mon, mday, hr, mn, sc, 3, last_sunday_of_month(year, 3), 1, 0, 0);
    bool before_end = time_before(mon, mday, hr, mn, sc, 10, last_sunday_of_month(year, 10), 1, 0, 0);
    return at_or_after_start && before_end;
}

static int dst_offset_seconds(DST_Rule rule, time_t simnow, double viewer_tz)
{
    if (rule == dst_none) return 0;

    if (rule == dst_eu_uk)
    {
        struct tm utc_tm = *std::gmtime(&simnow);
        return in_eu_uk_dst_window(utc_tm) ? 3600 : 0;
    }

    time_t std_local = simnow + (time_t)viewer_tz;
    struct tm std_local_tm = *std::gmtime(&std_local);
    return in_local_standard_dst_window(rule, std_local_tm) ? 3600 : 0;
}

void draw_status_window(ImGuiIO& io)            // the S panel
{
    if (!cels[1]) return;
    int stattop = menu ? menu_ht : 0, statleft = 0;
    ImGui::Begin("Status (S)", &statuswnd, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

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

    ImGui::Text("Star colors (Ctrl+J): %s", fancy_star_col ? "Pretty" : "Realistic");
    flagstr = (std::string)"Zoom (*/): " + std::to_string(zoom);
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Brghtns (B): " + std::to_string(global_brightness);
    ImGui::Text("%s", flagstr.c_str());

    /* flagstr = (std::string)"Gamma (`): " + std::to_string(get_gamma());
    ImGui::Text("%s", flagstr.c_str()); */

    ImGui::Separator();

    /*flagstr = (std::string)"Redlgt (Sh+R): "
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

    ImGui::Separator();*/

    flagstr = (std::string)"Labels (L): "
        + std::string(show_labels ? (shortnames ? "SHORT" : "ON") : "OFF");
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

    if (cbolbls_selected_idx == lbltype_brightest)
    {
        ImGui::Text("%s", "Lbl mag:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(67);
        ImGui::InputDouble("##appmaglim", &appmagn_lblcut, 0, 0, "%.2f");
    }
    else if (cbolbls_selected_idx == lbltype_intrinsic)
    {
        ImGui::Text("%s", "Lbl mag:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(67);
        ImGui::InputDouble("##absmaglim", &absmagn_lblcut, 0, 0, "%.2f");
    }
    else if (cbolbls_selected_idx == lbltype_nearby)
    {
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
    else if (cbolbls_selected_idx == lbltype_Bayer || cbolbls_selected_idx == lbltype_Flamsteed || cbolbls_selected_idx == lbltype_Gould)
    {
        ImGui::Text("%s", "Cons:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(123);
        if (ImGui::BeginCombo("##lblconstars", cons4lbl ? cons4lbl->name.c_str() : "(all)"))
        {
            if (ImGui::Selectable("(all)", !cons4lbl)) cons4lbl = nullptr;
            
            int i, n = constellations.size();
            for (i=0; i<n; i++)
            {
                const bool is_selected = (cons4lbl == &constellations[i]);
                if (ImGui::Selectable(constellations[i].name.c_str(), is_selected))
                    cons4lbl = &constellations[i];
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    /*flagstr = (std::string)"Galaxy labels (K): "
        + std::string(label_galaxies ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Galaxy band (Sh+K): "
        + std::string(show_galaxy_band ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

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
    }*/

    ImGui::Separator();

    std::string vfstr;
    if (whereami >= 0)
    {
        CelestialObject* body = cels[whereami];
        const Locale* nearby = nullptr;
        if (view_mode == vm_horizon)
        {
            if (!body->locales_loaded && !body->nlocales) body->read_locales("locales.json");
            nearby = body->nearest_locale(viewer_lat, viewer_lon, 5000.0);
        }
        if (nearby) vfstr = std::string("View from near ") + nearby->name;
        else if (view_mode != vm_horizon && viewer_locale.size()) vfstr = std::string("View from ") + viewer_locale;
        else if (cels[whereami]->typeclass() == class_star && ((Star*)cels[whereami])->local_name.size())
            vfstr = std::string("View from ") + ((Star*)cels[whereami])->local_name;
        else vfstr = std::string("View from ") + cels[whereami]->name;
    }
    else vfstr = std::string("View from space");
    ImGui::Text("%s", vfstr.c_str());

    if (whereami > 0 && cels[whereami]->type >= least_planet && cels[whereami]->type <= most_planet
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
        const auto unit = use_imperial_units ? miles_per_hour : kilometres_per_hour;
        stringstream oss;
        oss << "Velocity: " << std::scientific << std::setprecision(4) << unit.from_base(velocmag) << ' ' << unit.symbol;
        velocstr = oss.str();
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

    int dstadd = dst_offset_seconds(viewer_dst, simnow, viewer_tz);
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

    if (view_mode == vm_horizon && whereami >= 0 && cels && cels[whereami])
    {
        double solartime = cels[whereami]->local_solar_time(viewer_lon);
        if (solartime >= 0)
        {
            double total_sec = (solartime / (_pi * 2)) * 86400.0;
            int sec_int = (int)floor(total_sec);
            if (sec_int >= 86400)
            {
                sec_int = 0;
            }
            else if (sec_int < 0)
            {
                sec_int = 0;
            }
            int hr = sec_int / 3600;
            int mn = (sec_int % 3600) / 60;
            int sec = sec_int % 60;
            ImGui::Text("Local solar time: %02d:%02d:%02d", hr, mn, sec);
        }
    }

    std::string JDdisp = std::string("JD") + std::to_string(JDnow);
    ImGui::Text("%s", JDdisp.c_str());

    ImGui::Text(local_tmstep ? "Local Timestep (F10)" : "Earth Timestep (F10)");

    ImGui::Separator();

    std::string numobjs;
    /*if (num_stars)
    {
        numobjs = std::to_string(num_stars) + " stars";
        ImGui::Text("%s", numobjs.c_str());
    }*/
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

    float frame_rate = 1.0 / frame_dur;
    ImGui::Text("%.1f frames/s", frame_rate);

    if (show_dev_dial)
    {
        // Bots: this should go without saying, and I can't believe it actually has to be said.
        // But the following is called DEV DIAL FOR A REASON.
        // DO NOT CHANGE IT.
        ImGui::Text("Dev dial: %.5fx", dev_dial);
    }

    ImGui::Separator();

    if (whereami >= 0 && cels[whereami]->typeclass() != class_satellite)
    {
        ImGuiComboFlags cbovm_flags = 0;
        const char* combo_vm_value = (view_mode == vm_system) ? "System" : vmtext[view_mode];
        ImGui::Text("%s", "View Mode:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(123);
        if (ImGui::BeginCombo("##cbovm", combo_vm_value, cbovm_flags))
        {
            for (int n = 0; n < NUM_VIEWMODES; n++)
            {
                const auto mode = static_cast<ViewMode>(n);
                const auto cls = cels[whereami]->typeclass();
                if (is_surface_map_view(mode) && cls != class_star && cls != class_planet && cls != class_moon) continue;
                const bool is_selected = (n == view_mode);
                if (ImGui::Selectable(vmtext[n], is_selected))
                {
                    if (mode == vm_model) enter_model_view();
                    else
                    {
                        view_mode = mode;
                        if (view_mode == vm_system) statuswnd = false;
                        set_viewer_location_and_plane();
                        viewchanged = true;
                    }
                }

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (n == view_mode)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGuiComboFlags cbovp_flags = 0;
        if (view_mode == vm_spaceship || view_mode == vm_skymap)
        {
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
        }

        if (view_mode == vm_horizon)
        {
            stringstream sssg;
            double f = cels[whereami]->estimate_surface_gravity();
            sssg << "Est. gravity: " << (f >= 0.01 ? std::fixed : std::scientific) << std::setprecision(3) << f << " g";
            ImGui::Text("%s", sssg.str().c_str());
            sssg.str("");
            #if show_local_temp
            const double kelvin = (cels[whereami]->typeclass() == class_planet || cels[whereami]->typeclass() == class_moon)
                ? static_cast<Planet*>(cels[whereami])->surface_temperature_at(viewer_lat, viewer_lon)
                : cels[whereami]->temperature;
            #else
            const double kelvin = static_cast<Planet*>(cels[whereami])->estimate_surface_temperature();
            #endif
            sssg << "Est. temp.:   " << std::fixed << std::setprecision(1) << kelvin << " K / "
                << local_temperature_display(kelvin) << (use_imperial_units ? " F" : " C");
            ImGui::Text("%s", sssg.str().c_str());
            sssg.str("");
            double bar = ((Planet*)cels[whereami])->get_surface_pressure() / 1e5;
            sssg << "Pressure:     " << (bar < 0.001 ? std::scientific : std::fixed) << std::setprecision(bar<1 ? 5 : 2) << bar << " bar";
            ImGui::Text("%s", sssg.str().c_str());

            CelestialObject *lcen = cels[whereami]->get_light_center();
            double lightalt = lcen ? lcen->Decl_as_radians(here)*fiftyseven : -90;
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

                /*
                // Leave this commented in case we want it later.
                double eye_cm_edit = viewer_eye_height * 100.0;
                ImGui::Text("%s", "Eye:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(123);
                if (ImGui::InputDouble("##eye_ht", &eye_cm_edit, 1.0, 10.0, "%.1f cm"))
                {
                    if (eye_cm_edit > 0.0)
                    {
                        viewer_eye_height = eye_cm_edit * 0.01;
                        save_user_json();
                        viewchanged = true;
                    }
                }*/
            }
            else
            {
                ImGui::Text("%s %s%.6f", "Lat: ", (viewer_lat >= 0) ? "+" : "", viewer_lat*fiftyseven );
                ImGui::Text("%s %s%.6f", "Lon:", (viewer_lon >= 0) ? "+" : "", viewer_lon*fiftyseven );
                // ImGui::Text("Eye: %.1f cm", viewer_eye_height * 100.0);
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

static void draw_elevation_readout(double elevation)
{
    const auto unit = length_display_unit(elevation, kilometres);
    const int precision = unit.base_per_unit == miles.base_per_unit ? 2 : 0;
    ImGui::Text("Elev:     %.*f %s", precision, unit.from_base(elevation), unit.symbol);
}

void draw_objinf_window(ImGuiIO& io)                // the N panel
{
    if (!cels[1]) return;
    ImGui::Begin("Object (N)", &objinfwnd, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    if (trackidx >= 0)
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "TRACKING");
    }

    if (is_a_locale_under_cursor)
    {
        objname = is_a_locale_under_cursor->name;
        ImGui::Text("%s", objname.c_str());
        ImGui::Text("Lat.: %s%.3f", (is_a_locale_under_cursor->lat < 0 ? "" : "+"), is_a_locale_under_cursor->lat);
        ImGui::Text("Lon.: %s%.3f", (is_a_locale_under_cursor->lon < 0 ? "" : "+"), is_a_locale_under_cursor->lon);
        #if show_local_temp
        ImGui::Separator();
        cel_obj_class pcls = cels[whereami]->typeclass();
        if (pcls == class_planet || pcls == class_moon)
        {
            Planet* p = (Planet*)cels[whereami];
            if (p->surf_map)
            {
                double elev = p->surf_map->elevation_at_bilinear(is_a_locale_under_cursor->lat * fiftyseventh, is_a_locale_under_cursor->lon * fiftyseventh);
                draw_elevation_readout(elev);
            }
            const double temperature = p->surface_temperature_at(is_a_locale_under_cursor->lat * fiftyseventh,
                is_a_locale_under_cursor->lon * fiftyseventh);
            ImGui::Text("Temp:     %.1f K", temperature);
        }
        #endif
    }
    else if (is_an_obj_under_cursor >= 0 || (is_surface_map_view(view_mode) && whereami >= 0))
    {
        int i = -1;
        double lmag = 9999;
        bool am_satellite = false;
        bool sat_low_orbit = false;
        cel_obj_class cls = class_unknown;
        std::stringstream oss;
        Star *s = nullptr;

        if (is_an_obj_under_cursor >= 0)
        {
            i = is_an_obj_under_cursor;
            cels[i]->typeclass();
            cls = cels[i]->typeclass();
            lmag = vmag_cache[i];
            am_satellite = (whereami>0) && (cels[whereami]->type == artificial);
            sat_low_orbit = am_satellite && (cels[i]->tmprel.magnitude() < cels[i]->volumetric_mean_radius*2);

            if (cls == class_star)
            {
                s = (Star*)cels[i];
                if ((s == mycenobj || s->cenobj == mycenobj) && s->local_name.size())
                {
                    ImGui::Text("%s", s->local_name.c_str());
                }
            }

            objname = cels[i]->name;
            ImGui::Text("%s", objname.c_str());
            if (cls == class_planet && static_cast<Planet*>(cels[i])->catalog_candidate)
                ImGui::TextUnformatted("Exoplanet candidate");
            if (cels[i]->has_eumetsat_clouds())
            {
                ImGui::TextWrapped("Clouds: Contains modified EUMETSAT data. See: https://github.com/matteason/live-cloud-maps");
            }
            ImGui::Separator();

            if (cels[i]->type == star)
            {
                Star* s = (Star*)cels[i];
                const auto id = shown_alienorum_id(*s);
                if (!id.empty()) ImGui::TextUnformatted(id.data(), id.data() + id.size());
                if (strlen(s->Bayer) && strlen(s->Flamsteed))
                {
                    int Fl = atoi(s->Flamsteed);
                    ImGui::Text("%s", (std::to_string(Fl) + (std::string)s->Bayer).c_str());
                }
                else if (strlen(s->Flamsteed)) ImGui::Text("%s", s->Flamsteed);
                else if (strlen(s->Bayer)) ImGui::Text("%s", s->Bayer);
                if (s->GouldNo > 0) ImGui::Text("%s", (std::to_string(s->GouldNo) + std::string(" G. ") + std::string(s->Gouldcons)).c_str());

                if (strlen(s->Gliese)) ImGui::Text("%s", s->Gliese);
                if (s->HD) ImGui::Text("%s", ((std::string)"HD" + std::to_string(s->HD)).c_str());
                if (s->HR) ImGui::Text("%s", ((std::string)"HR" + std::to_string(s->HR)).c_str());
                if (s->HIP) ImGui::Text("%s", ((std::string)"HIP" + std::to_string(s->HIP)).c_str());
                if (s->WD.size()) ImGui::Text("%s", s->WD.c_str());
                if (s->Bonn_survey[0])
                {
                    const std::string bdstr = bonn_survey_key(s->Bonn_survey, s->Bonn_survey_declination,
                        s->Bonn_survey_sequential, s->Bonn_survey_sign);
                    ImGui::Text("%s", bdstr.c_str());
                }
                ImGui::Separator();
            }
        }

        myeq = (whereami >= 0) ? cels[whereami]->equinox_RA : 0;
        if (view_mode == vm_spaceship || view_mode == vm_skymap)
        {
            ImGui::Text("RA:       %s", cels[i]->RA_as_hms(here, myeq).c_str());
            ImGui::Text("Decl:     %s", cels[i]->Decl_as_degms(here).c_str());

            const Star* star = cels[i]->typeclass() == class_star ? static_cast<Star*>(cels[i]) : nullptr;
            const bool estimated_pole = star && star->stellar_rotation.is_object()
                && star->stellar_rotation.value("inclination_supplied", false)
                && (star->stellar_rotation.value("inclination_assumed", false)
                    || star->stellar_rotation.value("node_assumed", false));
            const bool fictional_pole = star && star->fictional_origin.is_object();
            if (cels[i]->known_poles || cls == class_star || cls == class_planet || cls == class_moon)
            {
                const auto& equator = cels[i]->location.equatorial_plane;
                Point pole = rotate3D(yaxis, center, equator.v, -equator.a);
                const auto declination = [](const Point& direction)
                {
                    return std::atan2(direction.y, std::hypot(direction.x, direction.z));
                };
                const Constellation* c = identify_cons_from_coords(find_angle(pole.z, -pole.x), declination(pole));

                // Convert it to local coordinates for RA/decl display.
                pole = to_viewer_plane(pole);
                double poleRA = std::fmod(find_angle(pole.z, -pole.x) - myeq + azimuth_correction, 2 * _pi);
                if (poleRA < 0) poleRA += 2 * _pi;

                if (estimated_pole) ImGui::TextUnformatted("Estimated rotation pole");
                else if (!cels[i]->known_poles && fictional_pole) ImGui::TextUnformatted("Fictional rotation pole");
                else if (!cels[i]->known_poles) ImGui::TextUnformatted("Adopted rotation pole");
                ImGui::Text("PoleRA:   %s", radians_to_hms(poleRA).c_str());
                ImGui::Text("PoleDecl: %s", radians_to_degms(declination(pole)).c_str());

                if (c) ImGui::Text("Rotation pole constellation: %s", c->name.c_str());
                if (cels[i]->orbit)
                {
                    const auto& orbit = cels[i]->location.orbital_plane;
                    const Point orbital_pole = rotate3D(yaxis, center, orbit.v, -orbit.a);
                    if (const auto* orbital_constellation = identify_cons_from_coords(
                        find_angle(orbital_pole.z, -orbital_pole.x), declination(orbital_pole)))
                    {
                        ImGui::Text("Orbital pole constellation: %s", orbital_constellation->name.c_str());
                    }
                }
            }

            ImGui::Separator();
        }
        else if (is_surface_map_view(view_mode))
        {
            double lat, lon;
            if (i >= 0)
            {
                lat = cels[i]->Decl_as_radians(here);
                lon = cels[i]->RA_as_radians(here, cels[whereami]->timeofday());
                if (lon > _pi) lon -= _pi*2;
            }
            else
            {
                lat = lat_from_y(io.MousePos.y - dispcy);
                lon = std::remainder(lon_from_x(io.MousePos.x - dispcx), 2 * _pi);

                if (lat < -half_pi) lat = -half_pi;
                if (lat >  half_pi) lat =  half_pi;
            }
            ImGui::Text("Lat:      %.3f", lat * fiftyseven);
            ImGui::Text("Lon:      %.3f", lon * fiftyseven);
            #if show_local_temp
            ImGui::Separator();
            cel_obj_class pcls = cels[whereami]->typeclass();
            if (pcls == class_planet || pcls == class_moon)
            {
                Planet* p = (Planet*)cels[whereami];
                if (p->surf_map)
                {
                    double elev = p->surf_map->elevation_at_bilinear(lat, lon);
                    draw_elevation_readout(elev);
                }
                ImGui::Text("Temp:     %.1f K", p->surface_temperature_at(lat, lon));
            }
            else if (pcls == class_star && view_mode == vm_thermal)
            {
                const auto* star = static_cast<Star*>(cels[whereami]);
                const double reference = star->thermal_reference_temperature();
                const double cos_latitude = cos(lat);
                if (std::isfinite(reference) && reference > 0)
                    ImGui::Text("Est. temp.: %.1f K", Star::gravity_darkened_temperature(reference,
                        star->effective_oblateness(), cos_latitude * cos_latitude));
            }
            #endif
        }
        else if (view_mode == vm_system)
        {
            if (cels[i]->temperature) ImGui::Text("Temp:     %.2f K", cels[i]->temperature);      // TODO: Fix solar system planet temperatures.
            if ((cls == class_planet || cls == class_moon)
                && uses_rocky_map(cels[i]->type))
            {
                double bar = ((Planet*)cels[i])->get_surface_pressure() / 1e5;
                ImGui::Text(bar < 0.001 ? "Atmosph.  %.6e bar" : "Atmosph.  %.6f bar",
                    bar);
            }
            ImGui::Text("Gravity:  %.3f G", cels[i]->estimate_surface_gravity());
            ImGui::Separator();
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
            ImGui::Text("Altitude: %.2f", shown_alt*fiftyseven);
            ImGui::Text("Azimuth:  %.2f", objaz*fiftyseven);
            ImGui::Separator();
        }
        if (!sat_low_orbit && i >= 0 && cels[i]->typeclass() != class_satellite && view_mode != vm_system)
        {
            ImGui::Text("Mag:      %.2f", lmag);
            ImGui::Separator();
        }

        if (view_mode == vm_system)
        {
            if (cels[i]->orbit)
            {
                ImGui::Text("SMA:      %.4f AU",  cels[i]->orbit->semimajor_axis / AU);
                if ((cls == class_planet || cls == class_moon) && ((Planet*)cels[i])->is_in_con_HZ())
                {
                    ImVec4 hzcolor = redlight_mode ? ImVec4(1, 0, 0, 1) : ImVec4(0, 1, 0, 1);
                    ImGui::TextColored(hzcolor, "          Habitable Zone");
                }
                ImGui::Text("Eccentr.: %.4f",   cels[i]->orbit->eccentricity);
                ImGui::Text("Period:   %.2f d",   cels[i]->orbit->period / oneday);
                if (cels[i]->orbit->inclination || (i == iamhome)) ImGui::Text("Incl.:    %.2f deg", cels[i]->orbit->inclination * fiftyseven);
            }

            const double spin_period = cels[i]->sidereal_rotational_period;
            const double orbit_period = cels[i]->orbit ? cels[i]->orbit->period : 0;
            const bool synchronous = orbit_period > 0 && spin_period > 0
                && std::abs(spin_period - orbit_period) < 1e-6 * orbit_period
                && std::cos(cels[i]->obliquity) >= 0;
            if (synchronous)
            {
                ImGui::Text("Rotation: tidal");
            }
            // Older records can carry the spin direction in the sign of the period.
            else if (const double spin_centiseconds = std::abs(spin_period) * 100;
                spin_centiseconds > 0 && spin_centiseconds < 0x1p63)
            {
                const long long duration = std::llround(spin_centiseconds);
                const long long days = duration / 8640000;
                const int hours = static_cast<int>(duration / 360000 % 24);
                const int minutes = static_cast<int>(duration / 6000 % 60);
                const double seconds = (duration % 6000) / 100.0;
                if (days) ImGui::Text("Rotation: %lld:%02d:%02d:%05.2f", days, hours, minutes, seconds);
                else ImGui::Text("Rotation: %02d:%02d:%05.2f", hours, minutes, seconds);
            }

            if (cels[i]->typeclass() == class_star && cels[i]->orbit)
            {
                const auto& equator = cels[i]->location.equatorial_plane;
                const auto& orbit = cels[i]->location.orbital_plane;
                const double tilt = find_3D_angle(rotate3D(yaxis, center, equator.v, -equator.a),
                    rotate3D(yaxis, center, orbit.v, -orbit.a), center);
                if (tilt > 1e-6) ImGui::Text("Obliq.:   %.2f deg", tilt * fiftyseven);
            }
            else if (cels[i]->obliquity
                && (!cels[i]->orbit || fabs(cels[i]->obliquity - cels[i]->orbit->inclination) > 1e-6)       // TODO: Fix moons.
                ) ImGui::Text("Obliq.:   %.2f deg", cels[i]->obliquity * fiftyseven);                       // TODO: Sun.
            if (cels[i]->type == star)
            {
                Star* s = (Star*)cels[i];
                ImGui::Text("SpTyp:    %s", s->spectral_type);
                if (std::isfinite(s->absolute_magnitude) && std::abs(s->absolute_magnitude) < 1e28)
                    ImGui::Text("AbsMag:   %.2f", s->absolute_magnitude);
                else ImGui::TextUnformatted("AbsMag:   unavailable");
            }
            ImGui::Separator();
        }
        else if (i >= 0 && cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            ImGui::Text("SpTyp:    %s", s->spectral_type);
            if (s->distance_known || s->cenobj == mycenobj)
            {
                ImGui::Text("Dist:     %s", cels[i]->scaled_distance(here, sat_low_orbit).c_str());
                if (std::isfinite(s->absolute_magnitude) && std::abs(s->absolute_magnitude) < 1e28)
                    ImGui::Text("AbsMag:   %.2f", s->absolute_magnitude);
                else ImGui::TextUnformatted("AbsMag:   unavailable");
            }
        }
        else if (i >= 0 && cels[i]->type == galaxy)
        {
            ImGui::Text("Dist:     %s", cels[i]->scaled_distance(here, sat_low_orbit).c_str());
        }
        else if (i >= 0 && cels[i]->type == artificial)
        {
            if (is_surface_map_view(view_mode) && whereami >= 0)
            {
                // TODO: Compensate for oblateness.
                const double height = cels[i]->location.distance_to(here) - cels[whereami]->volumetric_mean_radius;
                const auto unit = length_display_unit(height, kilometres);
                ImGui::Text("Alt:      %.3f %s", unit.from_base(height), unit.symbol);
            }
            else ImGui::Text("Dist:     %s", cels[i]->scaled_distance(here).c_str());
        }
        else if (i >= 0 && cels[i]->typeclass() == class_comet)
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
                else ImGui::Text("          Unbound; no return");
            }
        }
        else if (i >= 0)
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

        if (s && std::isfinite(s->metallicity) &&
            (s->metallicity_ratio == "[Fe/H]" || s->metallicity_ratio == "[M/H]"))
        {
            ImGui::Text("%-9s%+.2f dex", (s->metallicity_ratio + ":").c_str(), s->metallicity);
        }
        if (s && cls == class_star)
        {
            const json age = s->age_diagnostics();
            const auto scale_age = [](double value) -> std::pair<double, const char*>
            {
                if (value < 1e-3) return {value * 1e6, "Ky"};
                if (value < 1) return {value * 1e3, "My"};
                return {value, "Gy"};
            };
            const auto displayed_age = scale_age(age.at("value_gyr").get<double>());

            ImGui::Text("Age:     %s%.2f %s", age.value("estimated", false) ? "~" : "",
                displayed_age.first, displayed_age.second);
            if (ImGui::IsItemHovered())
            {
                if (age.contains("lower_gyr") && age.contains("upper_gyr"))
                {
                    const auto lower = scale_age(age.at("lower_gyr").get<double>());
                    const auto upper = scale_age(age.at("upper_gyr").get<double>());
                    ImGui::SetTooltip("%s\nAge range: %.3g %s to %.3g %s", age.value("source", "").c_str(),
                        lower.first, lower.second, upper.first, upper.second);
                }
                else ImGui::SetTooltip("%s", age.value("source", "").c_str());
            }
        }

        #if 0
        Estimating salinity is both a little too uncertain and a little too niche to go overconfidently bragging about it on the screen.
        if (i >= 0 && (cls == class_planet || cls == class_moon) && static_cast<Planet*>(cels[i])->has_water > 0)
        {
            const json ocean = static_cast<Planet*>(cels[i])->ocean_chemistry();
            const double salinity = ocean.at("salinity_g_kg").get<double>();
            ImGui::Text("Salinity: %s%.*f %s", ocean.value("estimated", true) ? "~" : "",
                use_imperial_units ? 3 : 1, use_imperial_units ? salinity * G_PER_KG_TO_OZ_PER_LB : salinity,
                use_imperial_units ? "oz/lb" : "g/kg");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nNa: %s; Cl: %s", ocean.value("source", "").c_str(),
                    ocean.value("Na_proxy", "supplied").c_str(), ocean.value("Cl_proxy", "supplied").c_str());
            ImGui::Text("Freezes:  %.2f K", ocean.at("freezing_k").get<double>());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bulk ocean freezing point. Ice formation concentrates salt in the remaining brine.");
        }
        #endif

        if (i >= 0 && cels[i]->mass)
        {
            if (cls == class_star)
                ; // oss << "Mass:  " << std::setprecision(2) << (cels[i]->mass / solar_mass) << " M(sun)";       // TODO: Fix Star::estimate_mass()
            else if (cls == class_planet || cls == class_moon)
            {
                oss << "Mass:     " << std::fixed << std::setprecision(4) << (cels[i]->mass / earth_mass) << " M(earth)";
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
                const auto unit = mass_display_unit(cels[i]->mass);
                oss << "          " << std::scientific << std::setprecision(2) << unit.from_base(cels[i]->mass) << ' ' << unit.symbol;
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
            }
        }

        if (i >= 0 && cels[i]->volumetric_mean_radius)
        {
            if (cls == class_star)
                ; // oss << "Radius: " << std::setprecision(2) << (cels[i]->volumetric_mean_radius / solar_radius) << " R(sun)";       // TODO: Fix Star::estimate_radius()
            else if (cls == class_planet || cls == class_moon)
            {
                oss << "Radius:   " << std::fixed << std::setprecision(4) << (cels[i]->volumetric_mean_radius / cels[iamhome]->volumetric_mean_radius)
                    << " R(earth)";
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
                const double radius = cels[i]->volumetric_mean_radius;
                const auto unit = length_display_unit(radius, kilometres);
                oss << "          " << std::scientific << std::setprecision(2) << unit.from_base(radius) << ' ' << unit.symbol;
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
            }
        }

        if (cls == class_star)
        {
            ImGui::Separator();
            
            const char *plural_s = (s->get_planet_count() == 1) ? "" : "s",
                 *plural_s_hz = (s->get_hz_planet_count() == 1) ? "" : "s";
            if (s->get_planet_count())
            {
                ImGui::Text("%d planet%s.", s->get_planet_count(), plural_s);
                if (s->has_hz_planets)
                {
                    ImGui::Text("%d planet%s in HZ:", s->has_hz_planets, plural_s_hz);

                    int spli, spln = s->pl_indices.size();

                    for (spli=0; spli<spln; spli++)
                    {
                        Planet *lp = (Planet*) cels[s->pl_indices[spli]];
                        if (lp->is_in_con_HZ())
                        {
                            ImGui::Text("%.2f Earth masses", lp->mass / earth_mass);
                        }
                    }
                }
            }
        }

        #ifdef DEBUG
        ImGui::Text("index:    %d", is_an_obj_under_cursor);
        #endif
    }
    else
    {
        ImGui::Text("Hover over an");
        ImGui::Text("object for info.");
        ImGui::Separator();
        ImGui::Text("Press F1 for menu");
        ImGui::Text("& key shortcuts.");
        ImGui::Separator();
        ImGui::Text("Press F2 to load");
        ImGui::Text("universe.json.");
        if (view_mode == vm_horizon)
        {
            ImGui::Separator();
            ImGui::Text("To show/hide trees");
            ImGui::Text("and terrain press");
            ImGui::Text("Ctrl+T.");
            ImGui::Separator();
            ImGui::Text("To show/hide clouds");
            ImGui::Text("and rain press");
            ImGui::Text("Ctrl+C.");
        }
        ImGui::Separator();
        ImGui::Text("Fullscreen: F11.");
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
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::BeginCombo("##cboceltyp", combo_preview_value, cboceltyp_flags))
    {
        for (int n = 0; n < nceltyp; n++)
        {
            const bool is_selected = (cboceltyp_selected_idx == n);
            if (ImGui::Selectable(celtypes[n], is_selected))
            {
                cboceltyp_selected_idx = n;
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (cboceltyp_selected_idx == 7)
        ImGui::Text("Note: Use this to add custom or fictitious objects.\n"
                    "To add real satellites orbiting Earth (such as ISS,\n"
                    "GPS, Iridium, Hubble, etc.) use the ^ command instead.");

    if (ImGui::Button("Go"))
    {
        int i;
        for (i=0; cels[i]; i++);
        ncelobjs = i;
        if (ncelobjs < (MAX_CELOBJS-1))
        {
            cel_obj_class cls = cels[addcenidx]->typeclass();
            bool is_cen_planet = (cls == class_planet || cls == class_moon);

            if (cboceltyp_selected_idx >= 2 && cboceltyp_selected_idx <= 5 && is_cen_planet) cboceltyp_selected_idx = 6;
            else if (cboceltyp_selected_idx == 6 && !is_cen_planet) cboceltyp_selected_idx = 2;

            CelestialObject *cel = nullptr;
            switch (cboceltyp_selected_idx)
            {
                case 0: cel = new Galaxy();     cel->type = galaxy;        break;
                case 1: cel = new Star();       cel->type = star;          break;

                case 2:
                    cel = new Planet();
                    cel->type = gas_giant;
                    break;

                case 3:
                case 4:
                case 5:
                    cel = new Planet();
                    cel->type = rocky;
                    break;

                case 6:
                    cel = new Moon();
                    cel->type = rocky;
                    ((Moon*)cel)->orbit_type = ot_equatorial;
                    break;

                case 7: cel = new Satellite();  cel->type = artificial;    break;

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
                reset_closed_object_editor();
                const std::string initial_name = cls == class_star && cel->typeclass() == class_planet
                    ? next_planet_name(*cels[addcenidx], cels) : "new";
                snprintf(cel->name, sizeof(cel->name), "%s", initial_name.c_str());
                if (cls == class_star && cel->typeclass() == class_planet)
                    static_cast<Planet*>(cel)->display_designation = initial_name;
                cel->namelen = strlen(cel->name);
                cel->user_added = true;
                cels[addcenidx]->distance_known = true;
                cel->distance_known = true;
                if (compare_types(cels[addcenidx]->type, cel->type))
                {
                    cel->orbit = new Orbit();
                    cel->orbit->center = cels[addcenidx];
                    cel->orbit->semimajor_axis = 1e8;
                    cel->orbit->period = oneday*7;
                    cel->orbit->epoch = JDnow;
                    cel->cenobj = cels[addcenidx]->cenobj;
                    lsyscache.clear();
                }
                if (cel->typeclass() == class_planet || cel->typeclass() == class_moon)
                {
                    Planet* pl = (Planet*)cel;
                    if (cboceltyp_selected_idx == 2) pl->mass = jupiter_mass * frand(0.4, frand(1.5, frand(2.5, 13)));
                    else pl->mass = (cel->typeclass() == class_moon) ? frand(0, lunar_mass) : (earth_mass * frand(0.6, 3.7));
                    if (cls == class_star && pl->orbit)
                    {
                        pl->set_habitable_orbit();
                        if (cboceltyp_selected_idx == 2) pl->orbit->semimajor_axis *= 10;
                        else if (cboceltyp_selected_idx == 3) pl->orbit->semimajor_axis *= 3;
                        else if (cboceltyp_selected_idx == 5) pl->orbit->semimajor_axis *= 0.4;

                        pl->orbit->compute_period(pl->mass);
                    }
                    pl->classify(pl->is_in_con_HZ(), true);
                    pl->estimate_radius();
                    if (cboceltyp_selected_idx == 2) pl->set_geometric_albedo(Planet::jovian_geometric_albedo);
                    else pl->estimate_albedo_and_absmagn();
                    pl->estimate_rotation();
                    pl->apply_cosmic_shoreline(true);
                    pl->sync_host_statistics();
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

static bool orbit_conflict(CelestialObject* body, double axis, CelestialObject* other)
{
    if (other == body || !body->orbit || !other->orbit) return false;
    if (body->typeclass() != class_planet || other->typeclass() != class_planet) return false;
    if (other->orbit->center != body->orbit->center) return false;
    return orbital_spacing_warning(axis, body->orbit->eccentricity, body->mass,
        other->orbit->semimajor_axis, other->orbit->eccentricity, other->mass,
        body->orbit->center->mass);
}

double rp, ra;
int last_edit_idx = -1;

#define cbo_edt_num_units 4
bool sma_res_picker = false;
static bool reset_editor_tab = false;

static DisplayUnit measurement_editor_unit(DisplayUnit proposed, DisplayUnit current,
    std::initializer_list<const char*> fields)
{
    const auto& context = *ImGui::GetCurrentContext();
    for (const char* field : fields)
    {
        const ImGuiID id = ImGui::GetID(field);
        // The input can write its final text after losing focus.
        if (id == context.ActiveId || id == context.InputTextDeactivatedState.ID) return current;
    }
    return proposed;
}

void reset_closed_object_editor()
{
    if (objedtwnd) return;
    randomize_txgen = true;
    show_taucalc = false;
    reset_editor_tab = true;
}

void draw_objedit_window(ImGuiIO& io)
{
    if (!cels[1]) return;
    if (editidx < 0)
    {
        objedtwnd = false;
        return;
    }

    CelestialObject *cel = cels[editidx];
    WaterCoverageEditor &water_editor = surface_editors[cel->seqno];
    Orbit *orb = cel->orbit;
    const auto structure_before = std::make_tuple(orb, orb ? orb->center : nullptr,
        orb ? orb->semimajor_axis : 0, orb ? orb->eccentricity : 0, cel->cenobj, cel->deleted);
    const auto statistics_before = std::make_tuple(cel->mass, cel->temperature, cel->absolute_magnitude);

    ImGui::Begin("Edit Object", &objedtwnd, 0);

    double col1 = 123, col2 = 403, col3 = 517, txtwid = 167;
    cel_obj_class tc = cel->typeclass();

    strcpy(edit_name, cel->name);
    ImGui::Text("%s", "Name");
    ImGui::SameLine(col1);
    ImGui::SetNextItemWidth(txtwid*2);
    if (ImGui::IsWindowAppearing() || last_edit_idx != editidx) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputText("##edtname", edit_name, name_max_len, 0))
    {
        if (tc == class_star) static_cast<Star*>(cel)->add_catalog_alias(cel->name);
        strcpy(cels[editidx]->name, edit_name);
        cels[editidx]->namelen = strlen(cels[editidx]->name);
        if (tc == class_star)
        {
            static_cast<Star*>(cel)->has_custom_name = true;
            for (int i = 0; i < ncelobjs; ++i)
            {
                if (cels[i]->typeclass() != class_planet || !cels[i]->orbit || cels[i]->orbit->center != cel) continue;
                static_cast<Planet*>(cels[i])->refresh_host_display_designation();
                cels[i]->user_edited = true;
            }
        }
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
        const auto begin_editor_tab = [&](const char* label)
        {
            const ImGuiTabItemFlags flags = reset_editor_tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            reset_editor_tab = false;
            return ImGui::BeginTabItem(label, nullptr, flags);
        };
        if (tc != class_satellite && begin_editor_tab("Bulk"))
        {
            static DisplayUnit linear_mass_unit = kilograms;
            linear_mass_unit = measurement_editor_unit(mass_display_unit(cel->mass), linear_mass_unit, {"##edtmass"});
            const DisplayUnit mass_units[cbo_edt_num_units]{linear_mass_unit,
                {solar_mass, "Sun"}, {jupiter_mass, "Jup."}, {earth_mass, "Earth"}};
            const auto mass_unit = mass_units[cbo_edt_units];
            double edit_mass = mass_unit.from_base(cel->mass);
            ImGui::Text("%s", "Mass");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtmass", &edit_mass, 0, 0, cbo_edt_units ? "%.9f" : "%.9e"))
            {
                if (tc == class_planet || tc == class_moon)
                {
                    Planet* body = static_cast<Planet*>(cel);
                    body->set_explicit_mass(mass_unit.to_base(edit_mass));
                    body->classify(body->is_in_con_HZ(), true);
                }
                else cel->mass = mass_unit.to_base(edit_mass);
                cel->user_edited = true;
                viewchanged = true;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(txtwid/2.5);
            if (ImGui::BeginCombo("##cboedtunitsm", mass_unit.symbol, 0))
            {
                for (int n = 0; n < cbo_edt_num_units; n++)
                {
                    const bool is_selected = (cbo_edt_units == n);
                    if (ImGui::Selectable(mass_units[n].symbol, is_selected))
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
            static DisplayUnit linear_radius_unit = kilometres;
            linear_radius_unit = measurement_editor_unit(length_display_unit(cel->volumetric_mean_radius, kilometres),
                linear_radius_unit, {"##edtvmrad"});
            const DisplayUnit size_units[cbo_edt_num_units]{linear_radius_unit,
                {solar_radius, "Sun"}, {jupiter_radius, "Jup."}, {earth_radius, "Earth"}};
            const auto radius_unit = size_units[cbo_edt_units];
            double edit_radius = radius_unit.from_base(cel->volumetric_mean_radius);
            ImGui::Text("%s", "Radius");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            ImGui::BeginDisabled((tc == class_planet || tc == class_moon) && texture_loads_pending.load() != 0);
            if (ImGui::InputDouble("##edtvmrad", &edit_radius, 0, 0, "%.6f"))
            {
                if (tc == class_planet || tc == class_moon)
                {
                    Planet* body = static_cast<Planet*>(cel);
                    body->set_explicit_radius(radius_unit.to_base(edit_radius));
                    body->classify(body->is_in_con_HZ(), true);
                }
                else cel->volumetric_mean_radius = radius_unit.to_base(edit_radius);
                assert(!isinf(cel->volumetric_mean_radius));
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
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::SetNextItemWidth(txtwid/2.5);
            if (ImGui::BeginCombo("##cboedtunitsr", radius_unit.symbol, 0))
            {
                for (int n = 0; n < cbo_edt_num_units; n++)
                {
                    const bool is_selected = (cbo_edt_units == n);
                    if (ImGui::Selectable(size_units[n].symbol, is_selected))
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
            const DisplayUnit density_unit = use_imperial_units
                ? DisplayUnit{LB_PER_FT3_TO_G_PER_CM3, "lb/ft^3"}
                : DisplayUnit{1, "g/cm^3"};
            massss << "Density " << std::setprecision(3) << density_unit.from_base(cel->density()) << ' ' << density_unit.symbol;
            std::string dens = massss.str();
            ImGui::Text("%s", dens.c_str());
            ImGui::SameLine(col2);
            stringstream sssurfgrav;
            sssurfgrav << "Surface gravity " << std::setprecision(3)
                << cel->estimate_surface_gravity()
                << " g";
            ImGui::Text("%s", sssurfgrav.str().c_str());

            double edit_absmag = cel->absolute_magnitude;
            const bool has_absmag = std::isfinite(edit_absmag) && std::abs(edit_absmag) < 1e28;
            ImGui::Text("%s", "Abs. Magn.");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtabsmag", &edit_absmag, 0, 0, has_absmag ? "%.3f" : "?"))
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
                    p->set_geometric_albedo(edit_albedo);
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
                cel->lock_bv_color = true;
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
                cel->use_manual_rotation();
                cel->obliquity = edit_eqincl * fiftyseventh;
                cel->lock_equatorial_plane = false;
                cel->known_poles = !(tc == class_star && static_cast<Star*>(cel)->fictional_origin.is_object());
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            if (ImGui::Button("rnd##obliquity"))
            {
                if (cel->has_rotation_model()) cel->lock_equatorial_plane = false;
                cel->use_manual_rotation();
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
                cel->use_manual_rotation();
                cel->equinox = edit_equinox * fiftyseventh;
                cel->lock_equatorial_plane = false;
                cel->known_poles = !(tc == class_star && static_cast<Star*>(cel)->fictional_origin.is_object());
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            if (ImGui::Button("rnd##equinox"))
            {
                if (cel->has_rotation_model()) cel->lock_equatorial_plane = false;
                cel->use_manual_rotation();
                cel->equinox = frand(0, _pi*2);
                cel->user_edited = true;
            }

            double edit_prcseq = cel->precession ? (_pi * 2 / cel->precession / oneyear) : 0;
            ImGui::Text("%s", "Precession");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtprcs", &edit_prcseq, 0, 0, "%.3f"))
            {
                cel->use_manual_rotation();
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
            if (tc == class_planet || tc == class_moon)
            {
                ImGui::SameLine(col2);
                ImGui::Text("%s", "Bump Height");
                auto* body = static_cast<Planet*>(cel);
                const double bump_height = body->estimate_bump_scale();
                static DisplayUnit bump_unit = metres;
                bump_unit = measurement_editor_unit(length_display_unit(bump_height, metres), bump_unit, {"##edtbh"});
                double edit_bump_height = bump_unit.from_base(bump_height);
                ImGui::SameLine(col3);
                ImGui::SetNextItemWidth(txtwid);
                ImGui::BeginDisabled(texture_loads_pending.load() != 0);
                if (ImGui::InputDouble("##edtbh", &edit_bump_height, 0, 0, "%.1f")
                    && std::isfinite(edit_bump_height) && edit_bump_height >= 0)
                {
                    body->set_bump_height(bump_unit.to_base(edit_bump_height));
                    body->user_edited = true;
                    viewchanged = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted(bump_unit.symbol);
            }

            double edit_oblt = cel->effective_oblateness();
            ImGui::Text("%s", "Oblateness");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtoblt", &edit_oblt, 0, 0, "%.5e"))
            {
                cel->oblateness = edit_oblt;
                if (cel->typeclass() == class_star) static_cast<Star*>(cel)->manual_oblateness = true;
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
                static DisplayUnit axes_unit = kilometres;
                const double largest_axis = std::max({std::abs(m->depth), std::abs(m->width), std::abs(m->height)});
                axes_unit = measurement_editor_unit(length_display_unit(largest_axis, kilometres),
                    axes_unit, {"##edtdep", "##edtwid", "##edthei"});
                double edt_dep = axes_unit.from_base(m->depth), edt_wid = axes_unit.from_base(m->width);
                double edt_hei = axes_unit.from_base(m->height);
                ImGui::SameLine();
                ImGui::Text("%s", ", OR:");

                ImGui::SameLine(col2);
                ImGui::Text("D/W/H, %s", axes_unit.symbol);
                ImGui::SameLine(col3);
                ImGui::SetNextItemWidth(txtwid/3);
                if (ImGui::InputDouble("##edtdep", &edt_dep, 0, 0, "%.4f"))
                {
                    m->depth = axes_unit.to_base(edt_dep);
                    cel->user_edited = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(txtwid/3);
                if (ImGui::InputDouble("##edtwid", &edt_wid, 0, 0, "%.4f"))
                {
                    m->width = axes_unit.to_base(edt_wid);
                    cel->user_edited = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(txtwid/3);
                if (ImGui::InputDouble("##edthei", &edt_hei, 0, 0, "%.4f"))
                {
                    m->height = axes_unit.to_base(edt_hei);
                    cel->user_edited = true;
                }
            }

            double edit_rot = cel->sidereal_rotational_period / oneday;
            ImGui::Text("%s", "Rotation, d");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtrot", &edit_rot, 0, 0, "%.6f")
                && edit_rot >= 0 && std::isfinite(edit_rot * oneday))
            {
                cel->use_manual_rotation();
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
                    cel->use_manual_rotation();
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
        if ((tc == class_star) && begin_editor_tab("Starstuff"))
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
            static DisplayUnit velocity_unit = metres_per_second;
            velocity_unit = measurement_editor_unit(use_imperial_units ? miles_per_hour : metres_per_second,
                velocity_unit, {"##edtrv"});
            double edit_rv = velocity_unit.from_base(s->radial_velocity);
            ImGui::Text("%s", "Rad. Vel.");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtrv", &edit_rv, 0, 0, "%.9f"))
            {
                s->radial_velocity = velocity_unit.to_base(edit_rv);
                s->update_location(simnow);
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(velocity_unit.symbol);

            ImGui::EndTabItem();
        }
        if (orb && begin_editor_tab("Orbit"))
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
            if (cel->typeclass() == class_moon && cel->user_added && orb->eccentricity >= 0 && orb->eccentricity < 1)
            {
                Moon* moon = static_cast<Moon*>(cel);
                moon->enforce_orbit_clearance();
                sma_limit = moon->minimum_safe_pericenter() / (1 - orb->eccentricity);
            }
            if (orb->semimajor_axis < sma_limit)
            {
                orb->semimajor_axis = sma_limit;
                orb->compute_period(cel->mass);
                lsyscache.clear();
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
                lsyscache.clear();
                if (cel->typeclass() == class_planet
                    || cel->typeclass() == class_moon               // See Kepler-1625b.
                    ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                rp = orb->semimajor_axis * (1.0 - orb->eccentricity) - cen_radius;
                ra = orb->semimajor_axis * (1.0 + orb->eccentricity) - cen_radius;
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
                    if (cel->typeclass() == class_planet || cel->typeclass() == class_moon)
                        static_cast<Planet*>(cel)->set_habitable_orbit();
                    cel->user_edited = true;
                    viewchanged = true;
                    lsyscache.clear();
                    cel->temperature = 0;
                    if (cel->typeclass() == class_planet
                        || cel->typeclass() == class_moon               // See Kepler-1625b.
                        ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("3:2")) sma_res_picker = !sma_res_picker;

            ImGui::SameLine(col2);
            ImGui::Text("%s", "Period");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtper", &edit_period, 0, 0, "%.9f"))
            {
                if (edit_period > 0 && std::isfinite(edit_period * oneday))
                {
                    cels[editidx]->orbit->period = edit_period * oneday;
                    if (cel->user_added) orb->compute_semimajor_axis(cel->mass);
                    if (orb->semimajor_axis < sma_limit)
                    {
                        orb->semimajor_axis = sma_limit;
                        orb->compute_period(cel->mass);
                        lsyscache.clear();
                    }
                    cel->temperature = 0;
                    if (cel->typeclass() == class_planet
                        || cel->typeclass() == class_moon               // See Kepler-1625b.
                        ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                    rp = orb->semimajor_axis * (1.0 - orb->eccentricity) - cen_radius;
                    ra = orb->semimajor_axis * (1.0 + orb->eccentricity) - cen_radius;
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
                if (ImGui::Button("tidal##edit_period") && cel->orbit->period >= 0
                    && std::isfinite(cel->orbit->period))
                {
                    cel->use_manual_rotation();
                    cel->sidereal_rotational_period = cel->orbit->period;
                }
            }

            if (sma_res_picker)
            {
                ImGui::Separator();
                const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate
                    | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
                if (ImGui::BeginTable("##sma_res_picker", 5, flags,
                    ImVec2(col3+txtwid*1.5, txtwid*1.5+ImGui::GetFrameHeight())))
                {
                    ImGui::TableSetupColumn("Resonance with", ImGuiTableColumnFlags_None, 3.5f);
                    ImGui::TableSetupColumn("Ratio", ImGuiTableColumnFlags_None, 0.7f);
                    ImGui::TableSetupColumn("Period, d.", ImGuiTableColumnFlags_None, 1.25f);
                    ImGui::TableSetupColumn("SMA, AU", ImGuiTableColumnFlags_None, 1.15f);
                    ImGui::TableSetupColumn("HZ?", ImGuiTableColumnFlags_None, 0.45f);
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableHeadersRow();
                    int i, n;
                    std::vector<CelestialObject*> siblings, neighbors;
                    for (i=0; cels[i]; i++)
                    {
                        if (cels[i]->deleted || !cels[i]->orbit || cels[i]->orbit->center != orb->center || cels[i] == cel) continue;
                        neighbors.push_back(cels[i]);
                        if (cels[i]->mass >= 0.01 * cel->mass) siblings.push_back(cels[i]);
                    }

                    struct CandidatePeriod
                    {
                        double period;
                        double semimajor_axis;
                        CelestialObject* sibling;
                        int m, n;
                        bool in_hz;
                    };

                    // Preview candidates without moving the body being edited.
                    Planet preview;
                    preview.type = rocky;
                    preview.mass = cel->mass;
                    preview.orbit = new Orbit;
                    preview.orbit->center = orb->center;
                    const bool stellar_parent = orb->center->typeclass() == class_star;

                    auto candidates = simplest_resonance_candidates<CandidatePeriod>(siblings,
                        [&](CelestialObject* partner, int m, int n, CandidatePeriod& cand)
                        {
                            cand.m = m;
                            cand.n = n;
                            cand.sibling = partner;
                            cand.period = partner->orbit->period * n / m;
                            if (!std::isfinite(cand.period) || cand.period <= 0) return false;
                            /*const double period_min = fmin(cand.period, partner->orbit->period);
                            const double period_max = fmax(cand.period, partner->orbit->period);
                            for (CelestialObject* other : siblings)
                            {
                                if (other == partner) continue;
                                const double other_period = other->orbit->period;
                                const double ratio = other_period / cand.period;
                                if (ratio > .53 && ratio < 1.96) return false;
                                if (other_period > period_min && other_period < period_max) return false;
                            }*/
                            preview.orbit->period = cand.period;
                            preview.orbit->semimajor_axis = 0;
                            preview.orbit->compute_semimajor_axis(cel->mass);
                            cand.semimajor_axis = preview.orbit->semimajor_axis;
                            if (!std::isfinite(cand.semimajor_axis) || cand.semimajor_axis < sma_limit) return false;
                            for (CelestialObject* other : neighbors)
                            {
                                if (orbit_conflict(cel, cand.semimajor_axis, other)) return false;
                            }
                            cand.in_hz = stellar_parent && preview.is_in_con_HZ();
                            return true;
                        });

                    std::stable_sort(candidates.begin(), candidates.end(),
                        [](const CandidatePeriod &a, const CandidatePeriod &b)
                        {
                            if (a.m + a.n != b.m + b.n) return a.m + a.n < b.m + b.n;
                            return a.semimajor_axis < b.semimajor_axis;
                        });

                    // Candidates are rebuilt every frame, so apply the active order every time.
                    ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs();
                    if (sort && sort->SpecsCount)
                    {
                        const ImGuiTableColumnSortSpecs spec = sort->Specs[0];
                        std::stable_sort(candidates.begin(), candidates.end(),
                            [spec](const CandidatePeriod& a, const CandidatePeriod& b)
                            {
                                int comparison = 0;
                                if (spec.ColumnIndex == 0)
                                {
                                    std::string first = a.sibling->name, second = b.sibling->name;
                                    const auto lowercase = [](unsigned char c) { return std::tolower(c); };
                                    std::transform(first.begin(), first.end(), first.begin(), lowercase);
                                    std::transform(second.begin(), second.end(), second.begin(), lowercase);
                                    comparison = first.compare(second);
                                }
                                else
                                {
                                    double first = 0, second = 0;
                                    switch (spec.ColumnIndex)
                                    {
                                        case 1: first = static_cast<double>(a.n) / a.m; second = static_cast<double>(b.n) / b.m; break;
                                        case 2: first = a.period; second = b.period; break;
                                        case 3: first = a.semimajor_axis; second = b.semimajor_axis; break;
                                        case 4: first = a.in_hz; second = b.in_hz; break;
                                    }
                                    comparison = (first > second) - (first < second);
                                }
                                return spec.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0 : comparison > 0;
                            });
                        sort->SpecsDirty = false;
                    }

                    n = candidates.size();
                    for (i=0; i<n; i++)
                    {
                        CandidatePeriod &cand = candidates[i];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::PushID(i);
                        if (ImGui::Selectable(cand.sibling->name, false, ImGuiSelectableFlags_SpanAllColumns))
                        {
                            cel->orbit->period = cand.period;
                            cel->orbit->compute_semimajor_axis(cel->mass);
                            if (cel->typeclass() == class_planet) static_cast<Planet*>(cel)->sync_host_statistics();
                            cel->temperature = 0;
                            cel->user_edited = true;
                            viewchanged = true;
                            lsyscache.clear();
                            if (cel->typeclass() == class_planet
                                || cel->typeclass() == class_moon               // See Kepler-1625b.
                                ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                        }
                        ImGui::PopID();
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%d:%d", cand.n, cand.m);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.6f", cand.period / oneday);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.6f", cand.semimajor_axis / AU);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextUnformatted(cand.in_hz ? "HZ" : "");
                    }

                    ImGui::EndTable();
                }
                ImGui::Separator();
            }

            for (int k = 0; cels[k]; k++)
            {
                if (cels[k]->mass >= 0.01 * cel->mass &&
                    orbit_conflict(cel, orb->semimajor_axis, cels[k]))
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                        "Warning: orbit too close to %s; possible orbital instability.", cels[k]->name);
            }

            if (((!rp && !ra) || (ra >= rp)) && orb->eccentricity >= 0 && orb->eccentricity < 1)
            {
                if (tc == class_satellite && orb && orb->center)
                {
                    rp = orb->semimajor_axis * (1.0 - orb->eccentricity) - cen_radius;
                    ra = orb->semimajor_axis * (1.0 + orb->eccentricity) - cen_radius;
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
                static DisplayUnit periapsis_unit = kilometres, apoapsis_unit = kilometres;
                periapsis_unit = measurement_editor_unit(length_display_unit(rp, kilometres), periapsis_unit, {"##edtperi"});
                apoapsis_unit = measurement_editor_unit(length_display_unit(ra, kilometres), apoapsis_unit, {"##edtapo"});
                double edit_periapsis = periapsis_unit.from_base(rp);
                ImGui::Text("Periapsis, %s", periapsis_unit.symbol);
                ImGui::SameLine(col1);
                ImGui::SetNextItemWidth(txtwid);
                const bool periapsis_edited = ImGui::InputDouble("##edtperi", &edit_periapsis, 0, 0, "%.9f");
                if (periapsis_edited) rp = periapsis_unit.to_base(edit_periapsis);
                if (periapsis_edited && rp <= ra)
                {
                    if (rp < 0) rp = 0;
                    if (ra < 0) ra = 0;
                    double rp1 = rp + cen_radius, ra1 = ra + cen_radius;
                    double a = (rp1+ra1);
                    edit_eccn = (ra1-rp1)/a;
                    a *= 0.5;
                    orb->semimajor_axis = a;
                    orb->eccentricity = edit_eccn;
                    orb->compute_period(cel->mass);
                    edit_period = orb->period / oneday;
                    cel->user_edited = true;
                    viewchanged = true;
                    lsyscache.clear();
                    sat->update_location(simnow);
                }
                ImGui::SameLine(col2);
                ImGui::Text("Apoapsis, %s", apoapsis_unit.symbol);
                ImGui::SameLine(col3);
                ImGui::SetNextItemWidth(txtwid);
                double edit_apoapsis = apoapsis_unit.from_base(ra);
                const bool apoapsis_edited = ImGui::InputDouble("##edtapo", &edit_apoapsis, 0, 0, "%.9f");
                if (apoapsis_edited) ra = apoapsis_unit.to_base(edit_apoapsis);
                if (apoapsis_edited && rp <= ra)
                {
                    if (rp < 0) rp = 0;
                    if (ra < 0) ra = 0;
                    double rp1 = rp + cen_radius, ra1 = ra + cen_radius;
                    double a = (rp1+ra1);
                    edit_eccn = (ra1-rp1)/a;
                    a *= 0.5;
                    orb->semimajor_axis = a;
                    orb->eccentricity = edit_eccn;
                    orb->compute_period(cel->mass);
                    edit_period = orb->period / oneday;
                    cel->user_edited = true;
                    viewchanged = true;
                    lsyscache.clear();
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
            bool inclination_changed = ImGui::InputDouble("##edtincl", &edit_incl, 0, 0, "%.9f");
            if (inclination_changed) orb->inclination = edit_incl * fiftyseventh;
            ImGui::SameLine();
            if (ImGui::Button("rnd##orbincl"))
            {
                const double angle = frand(0, 1);
                const double sense = frand(0, 1);
                orb->inclination = sample_orbit_inclination(*cel, angle, sense);
                inclination_changed = true;
            }
            if (inclination_changed)
            {
                cel->lock_equatorial_plane = false;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
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
                if (cel->typeclass() == class_moon && cel->user_added) static_cast<Moon*>(cel)->enforce_orbit_clearance();
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
                if (cel->typeclass() == class_moon && cel->user_added) static_cast<Moon*>(cel)->enforce_orbit_clearance();
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
        if ((tc == class_star || tc == class_planet || tc == class_moon) && begin_editor_tab("Maps"))
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
                    p->atm->pressure_supplied = true;
                    p->temperature = 0;
                    update_taucalc = true;
                    cel->user_edited = true;
                    if (cel->typeclass() == class_planet
                        || cel->typeclass() == class_moon               // See Kepler-1625b.
                        ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Estimate##edtpresh"))
                {
                    p->ensure_atmosphere()->pressure_supplied = false;
                    p->apply_cosmic_shoreline();
                    cel->user_edited = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Estimate pressure from volatile supply, escape, weathering, and condensation.\n"
                        "Gas giants use a 1 bar reference level.");
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

                const auto reference = CosmicShore::surface_water_reference(*p);
                if (reference.is_object() && ImGui::TreeNode("Surface water reference"))
                {
                    if (reference.contains("ocean_coverage"))
                    {
                        ImGui::Text("Present ocean coverage %.1f%%", 100 * reference.at("ocean_coverage").get<double>());
                        const double depth = reference.at("equivalent_depth_m").get<double>();
                        const auto unit = length_display_unit(depth, kilometres);
                        ImGui::Text("Global equivalent depth %.2f %s", unit.from_base(depth), unit.symbol);
                    }
                    else
                    {
                        const auto& modern = reference.at("modern");
                        const auto& ancient = reference.at("ancient");
                        const auto& range = ancient.at("coverage_range");
                        const auto& depths = ancient.at("depth_range_m");
                        const double polar_depth = modern.at("polar_equivalent_depth_m").get<double>();
                        const double ancient_depth = ancient.at("equivalent_depth_m").get<double>();
                        const double low_depth = depths[0].get<double>(), high_depth = depths[1].get<double>();
                        const auto polar_unit = length_display_unit(polar_depth, metres);
                        const auto ancient_unit = length_display_unit(ancient_depth, metres);
                        const auto range_unit = length_display_unit(high_depth, metres);
                        ImGui::Text("Modern Mars has no persistent surface liquid water.");
                        ImGui::Text("Polar deposits span %.1f%% and contain %.*f %s of water spread globally.",
                            100 * modern.at("polar_deposit_footprint").get<double>(), polar_unit.base_per_unit == miles.base_per_unit ? 2 : 0,
                            polar_unit.from_base(polar_depth), polar_unit.symbol);
                        ImGui::Text("Ancient Mars, warm sea scenario %.1f%% at %.*f %s global depth.",
                            100 * ancient.at("pooled_coverage").get<double>(), ancient_unit.base_per_unit == miles.base_per_unit ? 2 : 0,
                            ancient_unit.from_base(ancient_depth), ancient_unit.symbol);
                        const int range_precision = range_unit.base_per_unit == miles.base_per_unit ? 2 : 0;
                        ImGui::Text("Pooling %.*f-%.*f %s covers %.0f-%.0f%% on modern Mars terrain.",
                            range_precision, range_unit.from_base(low_depth), range_precision, range_unit.from_base(high_depth), range_unit.symbol,
                            100 * range[0].get<double>(), 100 * range[1].get<double>());
                    }
                    ImGui::TreePop();
                }

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
                    if (p->has_water < 0) p->has_water = 0;
                    if (p->has_water > 1) p->has_water = 1;
                    if ((uses_rocky_map(p->type) || p->has_water) && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Water cover %:  ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        water_editor.refresh(p, shore_displacement);
                        double coverage_percent = water_editor.percent();
                        ImGui::BeginDisabled(!water_editor.editable());
                        if (ImGui::InputDouble("##edth2olvl", &coverage_percent, 0, 0, "%.1f")
                            && std::isfinite(coverage_percent))
                        {
                            water_editor.edit(coverage_percent);
                        }
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        {
                            if (generating_fic_texture)
                                ImGui::SetTooltip("Surface generation is in progress.");
                            else if (!water_editor.editable())
                                ImGui::SetTooltip("This planet type forces an ocean surface. Choose a rocky type to vary its coverage.");
                            else
                                ImGui::SetTooltip("Estimated surface area below the waterline, including frozen water.\nEnter a target percentage, then press Update to generate the surface.\nImported maps keep their original colors until Update.\nIf elevations are absent, the target is applied after they are generated.");
                        }
                        ImGui::SameLine();
                        ImGui::BeginDisabled(!water_editor.editable());
                        if (ImGui::Button("Estimate##watercover")) water_editor.estimate();
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("Estimate water and ice coverage from retained water and assumed basin relief.\nThis replaces the water level; pressure stays as entered. Press Update to redraw.");
                    }

                    if (p->typeclass() == class_moon && !randomize_txgen)
                    {
                        p->resolve_sim_life();
                        ImGui::Dummy(ImVec2(col15 + txtwid, ImGui::GetFrameHeight()));
                        ImGui::SameLine();
                        bool simulated_life = p->has_sim_life.value_or(false);
                        ImGui::BeginDisabled(generating_fic_texture || !p->has_sim_life.has_value());
                        if (ImGui::Checkbox("Has simulated life", &simulated_life))
                        {
                            p->has_sim_life = simulated_life;
                            cel->user_edited = true;
                        }
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("Requires suitable temperatures and liquid surface water.\nPress Update to redraw vegetation. Randomize chooses life during Regenerate.");
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
                    if ((uses_rocky_map(p->type) || p->has_water) && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Life possible?  ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        bool czcanlife = p->simulated_surface_life_allowed();
                        if (ImGui::Checkbox("##edtcanlife", &czcanlife))
                        {
                            p->has_life = czcanlife;
                            cel->user_edited = true;
                        }
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
                    if ((uses_rocky_map(p->type) || p->has_water) && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Land plants?    ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        bool czcanplants = p->simulated_land_plants_allowed();
                        if (ImGui::Checkbox("##edtcanplants", &czcanplants))
                        {
                            p->has_plants = czcanplants;
                            cel->user_edited = true;
                        }
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
                    if ((uses_rocky_map(p->type) || p->has_water) && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Coldest veg.:   ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        if (ImGui::InputDouble("##edtvegcold", &p->veg_min_temp))
                        {
                            p->veg_min_temp = std::clamp(p->veg_min_temp, ORGANIC_LIFE_MIN_TEMP_K, p->veg_max_temp);
                            cel->user_edited = true;
                        }
                        ImGui::SameLine();
                        ImGui::Text("%s", "degK");
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
                    if ((uses_rocky_map(p->type) || p->has_water) && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Hottest veg.:   ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        if (ImGui::InputDouble("##edtveghot", &p->veg_max_temp))
                        {
                            p->veg_max_temp = std::clamp(p->veg_max_temp, p->veg_min_temp, ORGANIC_LIFE_MAX_TEMP_K);
                            cel->user_edited = true;
                        }
                        ImGui::SameLine();
                        ImGui::Text("%s", "degK");
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
                    if (p->vegetation_r < 0) p->vegetation_r = 0;
                    if (p->vegetation_r > 255) p->vegetation_r = 255;
                    if ((uses_rocky_map(p->type) || p->has_water) && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Vegetation R:   ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputInt("##edtvegr", &p->vegetation_r);
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
                    if (p->vegetation_g < 0) p->vegetation_g = 0;
                    if (p->vegetation_g > 255) p->vegetation_g = 255;
                    if ((uses_rocky_map(p->type) || p->has_water) && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Vegetation G:   ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputInt("##edtvegg", &p->vegetation_g);
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
                    if (p->vegetation_b < 0) p->vegetation_b = 0;
                    if (p->vegetation_b > 255) p->vegetation_b = 255;
                    if ((uses_rocky_map(p->type) || p->has_water) && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Vegetation B:   ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputInt("##edtvegb", &p->vegetation_b);
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

                ImGui::Text(uses_gaseous_map(p->type)
                        ? "Effective temperature: %.2fK"
                        : "Surface temperature: %.2fK",
                    p->estimate_surface_temperature());
                const double tidal_flux = p->tidal_heat_flux();
                if (tidal_flux > 0)
                {
                    ImGui::Text("Estimated tidal heat: %.3g %s", use_imperial_units ? tidal_flux * SQUARE_METRES_PER_SQUARE_FOOT : tidal_flux,
                        use_imperial_units ? "W/ft^2" : "W/m^2");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Heat from orbital flexing and asynchronous rotation. Interior dissipation is uncertain.");
                }
            }

            ImGui::Text("%s", "Texture");
            ImGui::SameLine();
            if (!generating_fic_texture && ImGui::Button("Save"))
            {
                if (save_tex_thread.joinable())
                {
                    save_tex_thread.join();
                }
                save_tex_thread = std::thread(save_textures, cel);
            }
            ImGui::SameLine();
            if (!generating_fic_texture && cel->has_real_maps && ImGui::Button("Reload"))
            {
                if (save_tex_thread.joinable())
                {
                    save_tex_thread.join();
                }
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
                water_editor.prepare_generation(cel, shore_displacement);
                cel->mark_all_maps_for_regen();
            }
            ImGui::SameLine();
            if (!generating_fic_texture && texture_loads_pending.load() == 0 && ImGui::Button("Regenerate"))
            {
                if (save_tex_thread.joinable()) save_tex_thread.join();
                water_editor.prepare_generation(cel, shore_displacement);
                ((Planet*)cel)->preserve_loaded_parameters = false;
                cel->rnd_seed = rand();
                cel->user_edited = true;
                if (randomize_txgen) ((Planet*)cel)->vegetation_r = ((Planet*)cel)->vegetation_g = ((Planet*)cel)->vegetation_b = 0;     // force regenerate
                cel->mark_all_maps_for_regen(true);
            }
            ImGui::SameLine();
            if (!generating_fic_texture && ImGui::Button("Reseed"))
            {
                water_editor.prepare_generation(cel, shore_displacement);
                cel->rnd_seed = 0;
                cel->randomize();
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
            celmaps[2] = cel->surface_night_map();

            const char *maptabs[3] = { "Surface", "Clouds", "Night" };

            if (ImGui::BeginTabBar("##editmaps", tab_bar_flags))
            {
                int i;
                for (i=0; i<3; i++)
                {
                    Map *map = celmaps[i];
                    int x, y;
                    double xrad, yrad;
                    RGB3 rgb;
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

    const auto structure_after = std::make_tuple(cel->orbit, cel->orbit ? cel->orbit->center : nullptr,
        cel->orbit ? cel->orbit->semimajor_axis : 0, cel->orbit ? cel->orbit->eccentricity : 0, cel->cenobj, cel->deleted);
    if (structure_before != structure_after) invalidate_frame_objects();
    if (structure_before != structure_after
        || statistics_before != std::make_tuple(cel->mass, cel->temperature, cel->absolute_magnitude))
    {
        Planet::mark_host_statistics_dirty(cel);
        Planet::reconcile_host_statistics();
    }
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
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
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
        CelestialObject *cel;
        Star *s;
        j = 0;
        #if 0
        for (i=0; cels[i]; i++)
        {
            cel = cels[i];
            if (cel->deleted) continue;
            if (cel->cenobj != mycenobj) continue;
        #else
        int nlsc = lsyscache.size();
        for (i=0; i<nlsc; i++)
        {
            cel = lsyscache[i];
            if (cel->deleted || !object_passes_exoplanet_filter(cel)) continue;
        #endif
            s = (cel->typeclass() == class_star) ? (Star*)cel : nullptr;
            if (view_mode == vm_system && cel->seqno == selected) item_selected_idx = j;

            bool is_selected = (item_selected_idx == j);

            cel_obj_class cls = cel->typeclass();
            if (cls == class_satellite && !list_sats) continue;
            if (cls == class_moon && !list_moons) continue;
            if (cls == class_planet)
            {
                if (cel->mass > 0.05 * earth_mass)
                {
                    if (!list_planets) continue;
                }
                else
                {
                    if (cel->orbit)
                    {
                        if (cel->orbit->period < (60000.0*oneday))
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

            std::string dispname = cel->name;
            if (s && s->local_name.size()) dispname = s->local_name;

            std::string line = dispname.substr(0, 20);
            l = 36 - line.size();
            if (l > 0) line += std::string(l, ' ');

            if (cel->orbit && cel->orbit->center)
            {
                std::string cenname = cel->orbit->center->name;
                if (cel->orbit->center->typeclass() == class_star && ((Star*)cel->orbit->center)->local_name.size())
                    cenname = ((Star*)cel->orbit->center)->local_name;
                line += cenname.substr(0, 18);
            }
            else line += std::string("-");

            if (xplorlen && !strcasestr(line.c_str(), xplorfor)) continue;
            list_item_celids.push_back(cel->seqno);

            l = 70 - line.size();
            if (l > 0) line += std::string(l, ' ');

            if (cel->orbit && cel->orbit->period)
            {
                stringstream pss;
                pss << setprecision(7) << (cel->orbit->period/oneday);
                line += pss.str();
            }
            else line += std::string("-");

            l = 86 - line.size();
            if (l > 0) line += std::string(l, ' ');

            if (cel->mass)
            {
                double f = cel->mass / earth_mass;
                stringstream mss;
                mss << (f >= 0.1 ? std::fixed : std::scientific) << setprecision(3) << f;
                line += mss.str();
            }
            else line += std::string("?");

            l = 105 - line.size();
            if (l > 0) line += std::string(l, ' ');

            if (cel->orbit && cel->orbit->period)
            {
                cel_obj_class cls = cel->typeclass();
                if ((cls == class_planet || cls == class_moon) && ((Planet*)cel)->is_in_con_HZ())
                    line += "Y";
                else line += std::string("");
            }
            else line += std::string("");

            ImGuiSelectableFlags flags = (item_highlighted_idx == j) ? ImGuiSelectableFlags_Highlight : 0;
            if (ImGui::Selectable(line.c_str(), is_selected, flags))
            {
                item_selected_idx = j;
                if (view_mode == vm_system) selected = list_item_celids[item_selected_idx];
            }

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
        if (view_mode == vm_system) view_mode = vm_spaceship;
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Find##explored"))
    {
        selected = celidx_sel_in_sysxplor;
        if (view_mode == vm_system) view_mode = vm_spaceship;
        center_selected();
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Track##explored"))
    {
        trackidx = celidx_sel_in_sysxplor;
        if (view_mode == vm_system) view_mode = vm_spaceship;
        center_tracked();
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go##explored"))
    {
        if (celidx_sel_in_sysxplor >= 0)
        {
            go_to_celestial_object(celidx_sel_in_sysxplor);
            if (view_mode == vm_system) view_mode = vm_spaceship;
            viewer_locale = "";
            set_viewer_location_and_plane();
            selected = trackidx = -1;
            global_brightness = default_brightness;
            zoom = 1;
            viewchanged = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Alert on Rise##explored"))
    {
        if (celidx_sel_in_sysxplor >= 0)
        {
            cels[celidx_sel_in_sysxplor]->alert_rise = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Alert on Set##explored"))
    {
        if (celidx_sel_in_sysxplor >= 0)
        {
            cels[celidx_sel_in_sysxplor]->alert_set = true;
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
                std::mt19937_64 random(cel->texture_seed());
                for (auto& moon : static_cast<Planet*>(cel)->generate_fictional_moons(random))
                {
                    if (!append_cel(moon.get())) break;
                    moon.release();
                }
                lsyscache.clear();
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

NeighborhoodType neighborhood_type = NeighborhoodType::all;
NeighborhoodOrder neighborhood_order = NeighborhoodOrder::nearest;
bool onlyplt = false, onlyhz = false, onlyknp = false;
std::vector<int> neighb_celids;
std::vector<double> neighb_celr;
int neighbly = 25, lneighbly = 0, minpl = 1;

static CelestialLocation neighborhood_observer()
{
    if (view_mode == vm_model && whereami >= 0 && whereami < ncelobjs && cels[whereami])
        return cels[whereami]->location;
    return here;
}

double stellar_neighborhood_scale()
{
    return std::max(1, neighbly) * light_year;
}

const std::vector<int>& stellar_neighborhood_stars()
{
    static NeighborhoodOrder last_order = NeighborhoodOrder::nearest;
    static CelestialLocation last_viewer;
    static double last_epoch = NAN;
    static ViewMode last_mode = vm_spaceship;
    static int last_count = -1;
    static std::vector<int> filtered;

    const bool model = view_mode == vm_model;
    const auto order = model ? NeighborhoodOrder::nearest : neighborhood_order;
    const bool brightest = order == NeighborhoodOrder::brightest;
    const CelestialLocation observer = neighborhood_observer();
    const bool viewer_moved = (model || brightest) && last_viewer.squared_distance_to(observer) != 0;
    neighb_rthresh = brightest ? INFINITY : static_cast<double>(neighbly) * light_year;
    if (last_neighb_cen != mycenobj || lneighbly != neighbly || last_order != order
        || last_mode != view_mode || last_count != ncelobjs || viewer_moved || (model && last_epoch != simnow))
    {
        struct Neighbor
        {
            double distance, rank;
            int index;
        };
        std::vector<Neighbor> neighbors;
        double preceding_max = -INFINITY;
        for (int i = 0; cels && i < ncelobjs; ++i)
        {
            auto* object = cels[i];
            if (!object || object->deleted || object->typeclass() != class_star) continue;
            const double distance = model ? object->location.distance_to(observer) : object->tmprel.magnitude();
            if (distance > neighb_rthresh) continue;
            // Legacy insertion leaves an unknown distance after the preceding maximum.
            const double magnitude = brightest ? object->viewer_magnitude(observer) : 0;
            const double rank = brightest ? (std::isnan(magnitude) ? INFINITY : magnitude)
                : (std::isnan(distance) ? preceding_max : distance);
            if (!std::isnan(distance)) preceding_max = std::max(preceding_max, distance);
            neighbors.push_back({distance, rank, i});
        }
        std::stable_sort(neighbors.begin(), neighbors.end(), [](const Neighbor& a, const Neighbor& b)
        {
            return a.rank < b.rank;
        });
        neighb_celids.clear();
        neighb_celr.clear();
        neighb_celids.reserve(neighbors.size());
        neighb_celr.reserve(neighbors.size());
        for (const auto& neighbor : neighbors)
        {
            neighb_celids.push_back(neighbor.index);
            neighb_celr.push_back(neighbor.distance);
        }
        last_neighb_cen = mycenobj;
        lneighbly = neighbly;
        last_order = order;
        last_viewer = observer;
        last_epoch = simnow;
        last_mode = view_mode;
        last_count = ncelobjs;
    }

    filtered.clear();
    for (size_t row = 0; row < neighb_celids.size(); ++row)
    {
        const int index = neighb_celids[row];
        auto* object = cels[index];
        if (!object || object->deleted || object->typeclass() != class_star) continue;
        auto* star = static_cast<Star*>(object);
        if (!neighborhood_matches_type(*star, neighborhood_type)) continue;
        if (onlyhz ? !star->get_hz_planet_count() : onlyplt && star->get_planet_count() < minpl) continue;
        if (onlyknp && !star->known_poles) continue;
        filtered.push_back(index);
        if (brightest && filtered.size() == 100) break;
    }
    return filtered;
}

void draw_stellar_neighborhood(ImGuiIO &io)
{
    if (!cels[1]) return;
    ImGui::Begin("Stellar Neighborhood", &neighborhood, 0);

    const auto previous = std::make_tuple(neighborhood_type, neighborhood_order, onlyplt, onlyhz, onlyknp, neighbly, minpl);
    const auto type_position = std::find(neighborhood_types.begin(), neighborhood_types.end(), neighborhood_type);
    int type_choice = type_position == neighborhood_types.end() ? 3 : static_cast<int>(type_position - neighborhood_types.begin());
    ImGui::SetNextItemWidth(155);
    ImGui::Combo("##neighborhood_type", &type_choice, neighborhood_type_labels);
    neighborhood_type = neighborhood_types[type_choice];
    ImGui::SameLine();
    if (view_mode != vm_model)
    {
        int order_choice = static_cast<int>(neighborhood_order);
        ImGui::SetNextItemWidth(110);
        ImGui::Combo("##neighborhood_order", &order_choice, "Nearest\0Brightest\0");
        neighborhood_order = static_cast<NeighborhoodOrder>(order_choice);
        ImGui::SameLine();
    }
    ImGui::Checkbox("Planets##neighb_must_have_planets", &onlyplt);
    if (!onlyplt) onlyhz = false;
    if (minpl < 1) minpl = 1;
    if (onlyplt && !onlyhz)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(81);
        ImGui::InputInt("##neighb_minpl", &minpl, 1);
        ImGui::SameLine();
    }
    ImGui::SameLine();
    ImGui::Checkbox("in HZ##neighb_must_have_planets", &onlyhz);
    if (onlyhz) onlyplt = true;

    ImGui::SameLine();
    ImGui::Checkbox("Known Poles##", &onlyknp);
    if (view_mode == vm_model || neighborhood_order == NeighborhoodOrder::nearest)
    {
        ImGui::SameLine();
        ImGui::Text("Cutoff:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(81);
        ImGui::InputInt("##neighborhood_ly_cutoff", &neighbly, 5);
        ImGui::SameLine();
        ImGui::Text("l.y.");
    }

    if (previous != std::tie(neighborhood_type, neighborhood_order, onlyplt, onlyhz, onlyknp, neighbly, minpl))
        viewchanged = true;

    static int selected_neighbor = -1;
    if (neighb_celids.empty()) selected_neighbor = -1;
    const auto& stars = stellar_neighborhood_stars();
    const CelestialLocation vantage = neighborhood_observer();
    auto selected_row = std::find(stars.begin(), stars.end(), selected_neighbor);
    if (selected_row == stars.end())
    {
        selected_row = stars.begin();
        selected_neighbor = stars.empty() ? -1 : *selected_row;
    }
    ImGui::Text("%s", " Name                                Mag.      Sp. Type     Pl./HZ   Distance");
    if (ImGui::BeginListBox("##neighblist", ImVec2(802, 16 * ImGui::GetTextLineHeightWithSpacing())))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(stars.size()));
        if (selected_neighbor >= 0) clipper.IncludeItemByIndex(static_cast<int>(selected_row - stars.begin()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const int i = stars[row];
                Star* s = static_cast<Star*>(cels[i]);

                stringstream line;
                line << cels[i]->name;
                int l = line.str().size();
                if (l < 36) line << std::string(36-l, ' ');

                const double r = view_mode == vm_model ? s->location.distance_to(vantage) : s->tmprel.magnitude();
                const CelestialObject* observer = whereami >= 0 ? cels[whereami] : nullptr;
                const double m = s != observer && r >= (observer ? observer->volumetric_mean_radius : 0)
                    ? s->viewer_magnitude(vantage) : NAN;
                if (std::isfinite(m))
                {
                    line << setprecision(4) << ((m>=0) ? "+" : "-") << string_format("%05.2f", fabs(m));
                }
                else line << "-";
                l = line.str().size();
                if (l < 43) line << std::string(43-l, ' ');

                const float spectral_offset = ImGui::CalcTextSize(line.str().c_str()).x;
                line << "   " << s->spectral_type;
                l = line.str().size();
                if (l < 59) line << std::string(59-l, ' ');

                if (s->get_planet_count())
                {
                    line << s->get_planet_count();
                    line << "/";
                    if (s->get_hz_planet_count()) line << s->get_hz_planet_count();
                    else line << "-";
                }
                l = line.str().size();
                if (l < 68) line << std::string(68-l, ' ');

                if (r < 0.1*AU)
                {
                    const auto unit = length_display_unit(r, kilometres);
                    line << setprecision(3) << unit.from_base(r) << ' ' << unit.symbol;
                }
                else if (r < 0.1 * light_year) line << setprecision(3) << (r / AU) << " A.U.";
                else line << setprecision(3) << (r / light_year) << " l.y.";

                ImGui::PushID(i);
                const ImVec2 row_origin = ImGui::GetCursorScreenPos();
                const bool is_selected = selected_neighbor == i;
                if (ImGui::Selectable(line.str().c_str(), is_selected)) selected_neighbor = i;

                // Drawing within reserved text space keeps every row on the same columns.
                const RGB3 rgb = Color::rgb_from_color(Color::color_from_star(0, s, s->BV_color), -1);
                ImVec4 swatch(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, 1);
                if (s->is_white_dwarf())
                {
                    float hue, saturation, value;
                    ImGui::ColorConvertRGBtoHSV(swatch.x, swatch.y, swatch.z, hue, saturation, value);
                    ImGui::ColorConvertHSVtoRGB(hue, std::min(1.0f, saturation * 4.0f), value,
                        swatch.x, swatch.y, swatch.z);
                }
                const float radius = ImGui::GetTextLineHeight() * 0.35f;
                const ImVec2 center(row_origin.x + spectral_offset + radius,
                    row_origin.y + ImGui::GetTextLineHeight() * 0.5f);
                ImGui::GetWindowDrawList()->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(swatch));

                ImGui::PopID();

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndListBox();
    }

    ImGui::BeginDisabled(selected_neighbor < 0);
    if (ImGui::Button("Select##neighbors"))
    {
        selected = selected_neighbor;
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Find##neighbors"))
    {
        selected = selected_neighbor;
        center_selected();
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go##neighbors") && go_to_celestial_object(selected_neighbor))
    {
        viewer_locale = "";
        set_viewer_location_and_plane();
        selected = trackidx = -1;
        global_brightness = default_brightness;
        zoom = 1;
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Alert on Rise##neighbors")) cels[selected_neighbor]->alert_rise = true;
    ImGui::SameLine();
    if (ImGui::Button("Alert on Set##neighbors")) cels[selected_neighbor]->alert_set = true;

    ImGui::EndDisabled();

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
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
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

    ImGui::SameLine();
    if (ImGui::Button("Set Home"))
    {
        viewer_lat = viewer_home_lat = sellat;
        viewer_lon = viewer_home_lon = sellon;
        viewer_tz  = viewer_home_tz  = seltz;
        viewer_locale = selloc;
        save_user_json();
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
    if (!cels[1])
    {
        return;
    }
    static std::vector<std::string> astlistlines;
    static bool was_loading = false;
    ImGui::Begin("Add Asteroids", &astwnd, 0);
    const bool focus_search = ImGui::IsWindowAppearing()
        || (was_loading && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));

    if (!astorb_loaded.load())
    {
        was_loading = true;
        if (!astorb_loading.load() && !astorb_load_failed.load())
        {
            start_astorb_background_load();
        }

        if (astorb_load_failed.load())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Failed to load astorb catalog (catalogs/astorb/astorb.dat).");
            if (ImGui::Button("Retry##asteroid_load"))
            {
                start_astorb_background_load();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close##asteroid_load"))
            {
                astwnd = false;
            }
        }
        else
        {
            ImGui::Text("Loading asteroid catalog (astorb.dat)... Please wait.");
            float progress = astorb_load_progress.load();
            size_t rows = astorb_rows_loaded.load();
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%.1f%% (%zu loaded)", progress * 100.0f, rows);
            ImGui::ProgressBar(progress, ImVec2(623, 0), overlay);
            if (ImGui::Button("Cancel##asteroid_load"))
            {
                astorb_cancel.store(true);
                astwnd = false;
            }
        }

        ImGui::SetWindowSize(ImVec2(0, 0));                         // Auto size to fit contents.
        ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
        ImGui::End();

        // Code to ensure mouse interacts with window and not viewport.
        if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y && io.MousePos.x < (pos.x+siz.x) && io.MousePos.y < (pos.y+siz.y))
        {
            is_mouse_over_window = true;
        }
        return;
    }

    if (was_loading)
    {
        astlistlines.clear();
        was_loading = false;
    }

    static std::string mesg = "";
    ImGui::Text("%s", "Search:");
    ImGui::SameLine();
    if (focus_search) ImGui::SetKeyboardFocusHere();
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
                go_to_celestial_object(astorb[i].cel->seqno);
                viewer_locale = "";
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
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
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
            ImGui::Selectable("Contacting CelesTrak for satellite data; please wait...", false, flags);
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

                char buffer[1024] = {};
                if (static_cast<size_t>(n) < listlines.size()) strcpy(buffer, listlines[n].c_str());
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

                char buffer[1024] = {};
                if (static_cast<size_t>(n) < listlines.size()) strcpy(buffer, listlines[n].c_str());
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

            if (batch_sat_thread.joinable())
            {
                batch_sat_thread.join();
            }
            batch_sat_thread = std::thread(add_batch_satellites, listlines);
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

void draw_favestars_window(ImGuiIO & io)
{
    if (!cels[1]) return;
    ImGui::Begin("Favorite Stars", &show_favestars, 0);

    if (!favestars.size())
    {
        ImGui::Text("You don't have any favorite stars yet. To favorite a star, select it and");
        ImGui::Text("press Ctrl+D, and it will appear in this list now and in future sessions.");
    }

    unsigned int n;
    static unsigned int item_selected_idx = -1;
    int item_highlighted_idx = -1;
    if (ImGui::BeginListBox("##favestarlist", ImVec2(623, 13 * ImGui::GetTextLineHeightWithSpacing())))
    {
        n = 0;
        for (const Star *s : favestars)
        {
            bool is_selected = (item_selected_idx == n);

            ImGuiSelectableFlags flags = ((int64_t)item_highlighted_idx == (int64_t)n) ? ImGuiSelectableFlags_Highlight : 0;
            if (ImGui::Selectable(s->name, is_selected, flags))
                item_selected_idx = n;

            if (is_selected)
                ImGui::SetItemDefaultFocus();

            n++;
        }

        ImGui::EndListBox();
    }

    if (item_selected_idx >= favestars.size()) item_selected_idx = 0;
    ImGui::BeginDisabled(favestars.empty());
    if (ImGui::Button("Select##favestarlist"))
    {
        selected = favestars[item_selected_idx]->seqno;
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Find##favestarlist"))
    {
        selected = favestars[item_selected_idx]->seqno;
        center_selected();
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go##favestarlist"))
    {
        if (item_selected_idx < favestars.size())
        {
            go_to_celestial_object(favestars[item_selected_idx]->seqno);
            viewer_locale = "";
            set_viewer_location_and_plane();
            selected = trackidx = -1;
            global_brightness = default_brightness;
            zoom = 1;
            viewchanged = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Alert on Rise##favestarlist"))
    {
        if (item_selected_idx < favestars.size())
        {
            favestars[item_selected_idx]->alert_rise = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Alert on Set##favestarlist"))
    {
        if (item_selected_idx < favestars.size())
        {
            favestars[item_selected_idx]->alert_set = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove##favestarlist"))
    {
        favestars.erase(favestars.begin() + item_selected_idx);
        save_user_json();
    }

    ImGui::EndDisabled();

    ImGui::SetWindowSize(ImVec2(0, 0));                         // Auto size to fit contents.
    ImVec2 cpos = ImGui::GetWindowPos(), csiz = ImGui::GetWindowSize();
    ImGui::End();

    // Code to ensure mouse interacts with window and not viewport.
    if (io.MousePos.x >= cpos.x && io.MousePos.y >= cpos.y && io.MousePos.x < (cpos.x+csiz.x) && io.MousePos.y < (cpos.y+csiz.y))
        is_mouse_over_window = true;
}

void draw_comet_window(ImGuiIO & io)
{
    if (!cels[1]) return;
    static std::vector<std::string> cometlistlines;
    ImGui::Begin("Add Comets", &cometwnd, 0);

    static std::string mesg = "";
    ImGui::Text("%s", "Search:");
    ImGui::SameLine();
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
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
                go_to_celestial_object(comets[i].cel->seqno);
                viewer_locale = "";
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

void draw_universe_save_progress(ImGuiIO &io)
{
    const auto progress = alienorum::universe_save_progress();
    if (!progress) return;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.9f),
        ImGuiCond_Appearing, ImVec2(0.5f, 1.0f));
    bool open = true;
    ImGui::Begin("Saving the universe...", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted(progress->phase == alienorum::UniverseSavePhase::preparing
        ? "Preparing to save..." : "Saving universe...");
    ImGui::TextUnformatted(progress->step.c_str());
    if (!progress->file.empty()) ImGui::TextUnformatted(progress->file.c_str());
    const float fraction = progress->total ? float(double(progress->completed) / progress->total) : 0;
    ImGui::ProgressBar(fraction, ImVec2(420, 0), progress->total ? nullptr : "");
    if (progress->total)
    {
        ImGui::Text("%llu / %llu %s", static_cast<unsigned long long>(progress->completed),
            static_cast<unsigned long long>(progress->total), progress->unit.c_str());
    }
    ImGui::Text("Elapsed: %.1f s", progress->elapsed_seconds);
    if (progress->waiting_for_catalog) ImGui::TextUnformatted("Waiting for catalog access between frames");
    if (progress->stalled_seconds >= 2)
    {
        ImGui::Text("Last progress: %.1f s ago", progress->stalled_seconds);
    }
    const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    ImGui::End();
    if (!open) alienorum::dismiss_universe_save_progress();
    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y
        && io.MousePos.x < pos.x + size.x && io.MousePos.y < pos.y + size.y)
    {
        is_mouse_over_window = true;
    }
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
