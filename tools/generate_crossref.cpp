#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sys/stat.h>
#include <cstdint>

const double _pi = 3.14159265358979323846;
const double half_pi = _pi * 0.5;
const double deg_to_rad = _pi / 180.0;
const double rad_to_deg = 180.0 / _pi;
const double V_band = 5.5e-7;
const double B_band = 4.4e-7;
const double invlogmagnbase = 1.0 / std::log(std::pow(100.0, 0.2));
const double h_planck = 6.62607015e-34;
const double c_light = 299792458.0;
const double k_boltzmann = 1.380649e-23;
const double sun_temp = 5778.0;

double blackbody_flux(double T, double wav)
{
    return (2.0 * h_planck * c_light * c_light / std::pow(wav, 5)) / (std::exp(h_planck * c_light / (wav * k_boltzmann * T)) - 1.0);
}

double bv_correction = 0.0;

double temperature_from_BV(double BV)
{
    auto bv_of = [](double T) -> double
    {
        return std::log(blackbody_flux(T, V_band) / blackbody_flux(T, B_band)) * invlogmagnbase - bv_correction;
    };

    double lo = 1000.0, hi = 200000.0;
    if (BV >= bv_of(lo)) return lo;
    if (BV <= bv_of(hi)) return hi;

    for (int i = 0; i < 50; i++)
    {
        double mid = 0.5 * (lo + hi);
        if (bv_of(mid) > BV) lo = mid;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

char get_color_code_from_temp(double tempK)
{
    if (tempK > 10000) return 'b';
    if (tempK >  7300) return 'c';
    if (tempK >  6000) return 'w';
    if (tempK >  5300) return 'y';
    if (tempK >  3900) return 'o';
    return 'r';
}

struct ConsBoundary
{
    double RA;
    double decl;
};

struct ConstellationData
{
    std::string abbrev;
    std::vector<ConsBoundary> bounds;
    double RA_center = 0.0;
    double decl_center = 0.0;

    void build_perimeter()
    {
        if (bounds.size() < 3) return;
        std::vector<ConsBoundary> perimeter;
        std::vector<ConsBoundary> unvisited = bounds;

        perimeter.push_back(unvisited.front());
        unvisited.erase(unvisited.begin());

        RA_center = perimeter[0].RA;
        decl_center = perimeter[0].decl;
        int ra_dec_div = 2;

        while (!unvisited.empty())
        {
            const auto& cur = perimeter.back();
            auto nearest = unvisited.begin();
            double min_d = 1e20;

            for (auto it = unvisited.begin(); it != unvisited.end(); ++it)
            {
                double d_ra = std::fabs(cur.RA - it->RA);
                if (d_ra > _pi) d_ra = 2.0 * _pi - d_ra;
                double d_dec = std::fabs(cur.decl - it->decl);
                double d = d_ra * d_ra + d_dec * d_dec;
                if (d < min_d)
                {
                    min_d = d;
                    nearest = it;
                }
            }

            perimeter.push_back(*nearest);
            unvisited.erase(nearest);

            double new_ra = perimeter.back().RA;
            double new_decl = perimeter.back().decl;
            if (new_ra < RA_center - _pi) new_ra += _pi * 2.0;
            else if (new_ra > RA_center + _pi) new_ra -= _pi * 2.0;

            double mul = 1.0 / ra_dec_div;
            RA_center = (1.0 - mul) * RA_center + mul * new_ra;
            decl_center = (1.0 - mul) * decl_center + mul * new_decl;
            ra_dec_div++;
        }
        bounds = perimeter;
    }
};

std::vector<ConstellationData> g_constellations;
std::unordered_map<std::string, std::string> g_upper_to_standard_cons;

bool point_in_cons(double s_ra, double s_decl, const std::vector<ConsBoundary>& bounds)
{
    bool inside = false;
    int n = (int)bounds.size();
    if (n < 3) return false;

    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        double decl_i = bounds[i].decl;
        double decl_j = bounds[j].decl;

        if ((decl_i > s_decl) != (decl_j > s_decl))
        {
            double ra_i = bounds[i].RA - s_ra;
            double ra_j = bounds[j].RA - s_ra;

            while (ra_i <= -_pi) ra_i += 2.0 * _pi;
            while (ra_i >   _pi) ra_i -= 2.0 * _pi;
            while (ra_j <= -_pi) ra_j += 2.0 * _pi;
            while (ra_j >   _pi) ra_j -= 2.0 * _pi;

            if (ra_j - ra_i > _pi) ra_j -= 2.0 * _pi;
            else if (ra_i - ra_j > _pi) ra_i -= 2.0 * _pi;

            double intersect_ra = ra_i + (s_decl - decl_i) / (decl_j - decl_i) * (ra_j - ra_i);

            if (intersect_ra > 0.0 && intersect_ra < _pi)
            {
                inside = !inside;
            }
        }
    }
    return inside;
}

static std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static double safe_stod(const std::string& s, double def = 0.0)
{
    std::string t = trim(s);
    if (t.empty()) return def;
    try
    {
        return std::stod(t);
    }
    catch (...)
    {
        return def;
    }
}

static int safe_stoi(const std::string& s, int def = 0)
{
    std::string t = trim(s);
    if (t.empty()) return def;
    try
    {
        return std::stoi(t);
    }
    catch (...)
    {
        return def;
    }
}

std::string standard_cons_abbrev(const std::string& upper_cons)
{
    std::string up = upper_cons;
    for (char& c : up) c = ::toupper(c);
    if (g_upper_to_standard_cons.find(up) != g_upper_to_standard_cons.end())
    {
        return g_upper_to_standard_cons[up];
    }
    return upper_cons;
}

std::string identify_constellation(double ra_rad, double dec_rad)
{
    std::string best_cons = "";
    double best_dist = 1e29;

    for (const auto& c : g_constellations)
    {
        if (c.bounds.empty()) continue;
        double d_ra = std::fabs(ra_rad - c.RA_center);
        if (d_ra > _pi) d_ra = 2.0 * _pi - d_ra;
        if (d_ra > half_pi) continue;

        if (point_in_cons(ra_rad, dec_rad, c.bounds))
        {
            double d_dec = std::fabs(dec_rad - c.decl_center);
            double dist = d_ra * d_ra + d_dec * d_dec;
            if (dist < best_dist)
            {
                best_dist = dist;
                best_cons = c.abbrev;
            }
        }
    }

    if (!best_cons.empty())
    {
        return standard_cons_abbrev(best_cons);
    }

    return (dec_rad > 0) ? "UMi" : "Oct";
}

struct StarRecord
{
    uint64_t gaia_id = 0;
    std::string tyc = "";
    int hip = 0;
    int hd = 0;
    std::string gliese = "";
    std::string bayer_flam = "";
    std::string gould = "";
    std::string gould_cons = "";
    double vmag = 99.0;
    char color = 'w';
    double ra_deg = 0.0;
    double dec_deg = 0.0;
    std::string cons = "";
    std::string orig_id = "";
    std::string orig_host_id = "";
    std::string assigned_id = "";
    int soles_index = -1;
    bool is_soles = false;
    char component = 0;
    int host_idx = -1;
};

int main()
{
    std::cout << "Starting Alienorum cross-reference and catalog generator (V < 10.0)..." << std::endl;

    // 1. Initialize blackbody BV correction
    bv_correction = std::log(blackbody_flux(sun_temp, V_band) / blackbody_flux(sun_temp, B_band)) * invlogmagnbase - 0.65;

    // 2. Load standard constellation abbreviations from consline.dat
    std::ifstream cl_in("consline.dat");
    if (cl_in.is_open())
    {
        std::string line;
        while (std::getline(cl_in, line))
        {
            if (line.size() > 1 && line[0] == '~')
            {
                size_t comma = line.find(',');
                if (comma != std::string::npos)
                {
                    std::string abbrev = trim(line.substr(1, comma - 1));
                    std::string up = abbrev;
                    for (char& c : up) c = ::toupper(c);
                    g_upper_to_standard_cons[up] = abbrev;
                }
            }
        }
        cl_in.close();
    }
    g_upper_to_standard_cons["SER1"] = "Ser";
    g_upper_to_standard_cons["SER2"] = "Ser";
    g_upper_to_standard_cons["SER"] = "Ser";

    // 3. Load constellation boundaries
    std::ifstream cbd_in("catalogs/CBD/bound_20.dat");
    if (!cbd_in.is_open())
    {
        std::cerr << "Failed to open catalogs/CBD/bound_20.dat" << std::endl;
        return 1;
    }

    std::map<std::string, ConstellationData> cons_map;
    std::string line;
    while (std::getline(cbd_in, line))
    {
        if (line.size() < 28) continue;
        double ra = safe_stod(line.substr(0, 11)) * deg_to_rad;
        double dec = safe_stod(line.substr(12, 11)) * deg_to_rad;
        std::string cst = trim(line.substr(24, 4));
        cons_map[cst].abbrev = cst;
        cons_map[cst].bounds.push_back({ra, dec});
    }
    cbd_in.close();

    for (auto& pair : cons_map)
    {
        pair.second.build_perimeter();
        g_constellations.push_back(pair.second);
    }
    std::cout << "Loaded " << g_constellations.size() << " constellations with perimeters." << std::endl;

    // 4. Load Gaia DR3 IDs from HGCA (catalogs/GAIA/catalog.dat)
    std::unordered_map<int, uint64_t> hip_to_gaia;
    std::ifstream hgca_in("catalogs/GAIA/catalog.dat");
    if (hgca_in.is_open())
    {
        while (std::getline(hgca_in, line))
        {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string hip_s, src_s;
            if (std::getline(ss, hip_s, '|') && std::getline(ss, src_s, '|'))
            {
                int hip = safe_stoi(hip_s);
                try
                {
                    uint64_t src = std::stoull(trim(src_s));
                    hip_to_gaia[hip] = src;
                }
                catch (...) {}
            }
        }
        hgca_in.close();
        std::cout << "Loaded " << hip_to_gaia.size() << " Gaia DR3 mappings from HGCA." << std::endl;
    }

    // 5. Load soles_alienorum.dat
    std::vector<std::string> soles_lines;
    std::ifstream soles_in("catalogs/soles_alienorum.dat");
    if (!soles_in.is_open())
    {
        std::cerr << "Failed to open catalogs/soles_alienorum.dat" << std::endl;
        return 1;
    }
    while (std::getline(soles_in, line))
    {
        soles_lines.push_back(line);
    }
    soles_in.close();
    std::cout << "Read " << soles_lines.size() << " lines from soles_alienorum.dat." << std::endl;

    // Parse soles stars
    std::vector<StarRecord> all_stars;
    std::unordered_map<int, int> hip_to_record_idx;
    std::unordered_map<int, int> hd_to_record_idx;
    std::unordered_map<std::string, int> orig_host_to_star_idx;

    for (size_t i = 0; i < soles_lines.size(); i++)
    {
        const std::string& l = soles_lines[i];
        if (l.empty() || l[0] == '#') continue;

        StarRecord rec;
        rec.soles_index = (int)i;
        rec.is_soles = true;

        std::string orig_id = trim(l.substr(0, 14));
        rec.orig_id = orig_id;

        // Parse words of orig_id
        std::stringstream ss(orig_id);
        std::vector<std::string> words;
        std::string w;
        while (ss >> w) words.push_back(w);

        if (words.size() >= 2 && words.back().size() == 1 && words.back()[0] >= 'B' && words.back()[0] <= 'Z')
        {
            rec.component = words.back()[0];
            std::string h_id = "";
            for (size_t wi = 0; wi < words.size() - 1; wi++)
            {
                if (wi > 0) h_id += " ";
                h_id += words[wi];
            }
            rec.orig_host_id = h_id;
        }

        double ra_deg = safe_stod(l.substr(55, 2)) * 15.0 + safe_stod(l.substr(58, 2)) * (15.0 / 60.0) + safe_stod(l.substr(61, 4)) * (15.0 / 3600.0);
        int sgndecl = (l[66] == '-') ? -1 : 1;
        double dec_deg = sgndecl * (safe_stod(l.substr(67, 2)) + safe_stod(l.substr(70, 2)) / 60.0 + safe_stod(l.substr(73, 2)) / 3600.0);

        rec.ra_deg = ra_deg;
        rec.dec_deg = dec_deg;
        rec.vmag = safe_stod(l.substr(76, 6), 99.0);

        double bv = safe_stod(l.substr(99, 6), 0.65);
        double tempK = temperature_from_BV(bv);
        rec.color = get_color_code_from_temp(tempK);

        // Identifiers from soles_alienorum.dat
        std::string hd_s = trim(l.substr(113, 6));
        if (!hd_s.empty()) rec.hd = safe_stoi(hd_s);
        std::string hip_s = trim(l.substr(120, 6));
        if (!hip_s.empty()) rec.hip = safe_stoi(hip_s);

        if (rec.hip > 0 && hip_to_gaia.find(rec.hip) != hip_to_gaia.end())
        {
            rec.gaia_id = hip_to_gaia[rec.hip];
        }

        // Gliese
        std::string gl_s = (l.size() >= 168) ? trim(l.substr(153, 15)) : "";
        if (gl_s.rfind("GJ ", 0) == 0) gl_s = trim(gl_s.substr(3));
        rec.gliese = gl_s;

        // Bayer & Flamsteed
        int flam_no = (l.size() >= 145) ? safe_stoi(l.substr(141, 4)) : 0;
        std::string bayer_s = (l.size() >= 152) ? trim(l.substr(145, 7)) : "";

        // Constellation: preserve existing abbreviation if present
        if (words.size() >= 2 && words[0] != "-26" && words[1].size() == 3)
        {
            rec.cons = standard_cons_abbrev(words[1]);
        }
        else
        {
            rec.cons = identify_constellation(rec.ra_deg * deg_to_rad, rec.dec_deg * deg_to_rad);
        }

        // Format Bayer_Flamsteed
        if (flam_no > 0 && !bayer_s.empty())
        {
            char bf_buf[64];
            snprintf(bf_buf, sizeof(bf_buf), "%2d %-11s", flam_no, bayer_s.c_str());
            rec.bayer_flam = bf_buf;
        }
        else if (flam_no > 0)
        {
            char bf_buf[64];
            snprintf(bf_buf, sizeof(bf_buf), "%2d     %-3s", flam_no, rec.cons.c_str());
            rec.bayer_flam = bf_buf;
        }
        else if (!bayer_s.empty())
        {
            char bf_buf[64];
            snprintf(bf_buf, sizeof(bf_buf), "   %-11s", bayer_s.c_str());
            rec.bayer_flam = bf_buf;
        }

        // Gould
        int gould_no = (l.size() >= 173) ? safe_stoi(l.substr(169, 4)) : 0;
        std::string gould_c = (l.size() >= 176) ? trim(l.substr(173, 3)) : "";
        if (gould_no > 0)
        {
            rec.gould = std::to_string(gould_no);
            if (!gould_c.empty() && gould_c != rec.cons)
            {
                rec.gould_cons = standard_cons_abbrev(gould_c);
            }
        }

        int idx = (int)all_stars.size();
        all_stars.push_back(rec);

        if (rec.hip > 0) hip_to_record_idx[rec.hip] = idx;
        if (rec.hd > 0) hd_to_record_idx[rec.hd] = idx;
        if (rec.component == 0 && !rec.orig_id.empty())
        {
            orig_host_to_star_idx[rec.orig_id] = idx;
        }
    }
    std::cout << "Parsed " << all_stars.size() << " stars from soles_alienorum.dat." << std::endl;

    // 6. Ingest Tycho catalog (catalogs/Hipparcos/tyc_main.dat)
    std::ifstream tyc_in("catalogs/Hipparcos/tyc_main.dat");
    if (!tyc_in.is_open())
    {
        std::cerr << "Failed to open catalogs/Hipparcos/tyc_main.dat" << std::endl;
        return 1;
    }

    size_t tyc_matched = 0;
    size_t tyc_added = 0;
    while (std::getline(tyc_in, line))
    {
        if (line.size() < 76) continue;
        std::string vm_s = trim(line.substr(41, 5));
        if (vm_s.empty()) continue;
        double vmag = safe_stod(vm_s, 99.0);
        if (vmag >= 10.0) continue; // Completeness ceiling V < 10.0!

        // Construct TYC ID
        std::string tyc1 = trim(line.substr(2, 4));
        std::string tyc2 = trim(line.substr(7, 5));
        std::string tyc3 = trim(line.substr(13, 1));
        std::string tyc_id = tyc1 + "-" + tyc2 + "-" + tyc3;

        int hip = (line.size() >= 216) ? safe_stoi(line.substr(210, 6)) : 0;
        int hd = (line.size() >= 315) ? safe_stoi(line.substr(309, 6)) : 0;

        double ra_deg = safe_stod(line.substr(51, 12));
        double dec_deg = safe_stod(line.substr(64, 12));

        double bv = (line.size() >= 251) ? safe_stod(line.substr(245, 6), 0.65) : 0.65;

        // Match existing star
        int matched_idx = -1;
        if (hip > 0 && hip_to_record_idx.find(hip) != hip_to_record_idx.end())
        {
            matched_idx = hip_to_record_idx[hip];
        }
        else if (hd > 0 && hd_to_record_idx.find(hd) != hd_to_record_idx.end())
        {
            matched_idx = hd_to_record_idx[hd];
        }

        if (matched_idx >= 0)
        {
            all_stars[matched_idx].tyc = tyc_id;
            if (all_stars[matched_idx].hd == 0) all_stars[matched_idx].hd = hd;
            if (all_stars[matched_idx].hip == 0) all_stars[matched_idx].hip = hip;
            tyc_matched++;
        }
        else
        {
            StarRecord new_rec;
            new_rec.tyc = tyc_id;
            new_rec.hip = hip;
            new_rec.hd = hd;
            new_rec.vmag = vmag;
            new_rec.ra_deg = ra_deg;
            new_rec.dec_deg = dec_deg;
            new_rec.color = get_color_code_from_temp(temperature_from_BV(bv));
            new_rec.cons = identify_constellation(ra_deg * deg_to_rad, dec_deg * deg_to_rad);
            if (hip > 0 && hip_to_gaia.find(hip) != hip_to_gaia.end())
            {
                new_rec.gaia_id = hip_to_gaia[hip];
            }

            int idx = (int)all_stars.size();
            all_stars.push_back(new_rec);
            if (hip > 0) hip_to_record_idx[hip] = idx;
            if (hd > 0) hd_to_record_idx[hd] = idx;
            tyc_added++;
        }
    }
    tyc_in.close();
    std::cout << "Tycho ingestion (V < 10.0): matched " << tyc_matched << " stars, added " << tyc_added << " new stars." << std::endl;

    // 7. Ingest saturated Hipparcos stars (catalogs/Hipparcos/hip_main.dat)
    std::ifstream hip_in("catalogs/Hipparcos/hip_main.dat");
    if (hip_in.is_open())
    {
        size_t hip_added = 0;
        while (std::getline(hip_in, line))
        {
            if (line.size() < 50) continue;
            std::stringstream ss(line);
            std::string token;
            std::vector<std::string> parts;
            while (std::getline(ss, token, '|')) parts.push_back(token);
            if (parts.size() < 6) continue;

            std::string hip_s = trim(parts[1]);
            std::string vm_s = trim(parts[5]);
            if (hip_s.empty() || vm_s.empty()) continue;
            int hip = safe_stoi(hip_s);
            double vmag = safe_stod(vm_s, 99.0);
            if (vmag >= 10.0) continue;

            if (hip_to_record_idx.find(hip) == hip_to_record_idx.end())
            {
                StarRecord new_rec;
                new_rec.hip = hip;
                new_rec.vmag = vmag;
                if (parts.size() >= 10)
                {
                    std::string ra_s = trim(parts[8]);
                    std::string dec_s = trim(parts[9]);
                    if (!ra_s.empty()) new_rec.ra_deg = safe_stod(ra_s);
                    if (!dec_s.empty()) new_rec.dec_deg = safe_stod(dec_s);
                }
                double bv = 0.65;
                if (parts.size() >= 38)
                {
                    std::string bv_s = trim(parts[37]);
                    if (!bv_s.empty()) bv = safe_stod(bv_s, 0.65);
                }
                new_rec.color = get_color_code_from_temp(temperature_from_BV(bv));
                new_rec.cons = identify_constellation(new_rec.ra_deg * deg_to_rad, new_rec.dec_deg * deg_to_rad);
                if (hip_to_gaia.find(hip) != hip_to_gaia.end()) new_rec.gaia_id = hip_to_gaia[hip];

                int idx = (int)all_stars.size();
                all_stars.push_back(new_rec);
                hip_to_record_idx[hip] = idx;
                hip_added++;
            }
        }
        hip_in.close();
        std::cout << "Added " << hip_added << " saturated bright Hipparcos stars." << std::endl;
    }

    // 8. Group all stars with V < 10.0 into bins: [cons][mag_int][color]
    std::cout << "Total distinct stars under magnitude 10: " << all_stars.size() << std::endl;

    std::map<std::string, std::map<int, std::map<char, std::vector<int>>>> bins;

    for (size_t i = 0; i < all_stars.size(); i++)
    {
        StarRecord& r = all_stars[i];
        if (r.vmag >= 10.0) continue;
        if (r.component > 0) continue;

        if (r.vmag < -20.0)
        {
            r.assigned_id = "-26";
            continue;
        }

        int m = (int)std::floor(r.vmag);
        bins[r.cons][m][r.color].push_back((int)i);
    }

    size_t total_binned = 0;
    for (auto& [cons, mag_map] : bins)
    {
        for (auto& [m, color_map] : mag_map)
        {
            for (auto& [color, star_indices] : color_map)
            {
                std::sort(star_indices.begin(), star_indices.end(), [&](int a, int b) {
                    if (std::fabs(all_stars[a].vmag - all_stars[b].vmag) > 1e-5)
                        return all_stars[a].vmag < all_stars[b].vmag;
                    return all_stars[a].ra_deg < all_stars[b].ra_deg;
                });

                size_t n = star_indices.size();
                for (size_t rank = 0; rank < n; rank++)
                {
                    int idx = star_indices[rank];
                    std::string id = std::to_string(m) + std::string(1, color) + " " + cons;
                    if (n > 1) id += " " + std::to_string(rank + 1);
                    all_stars[idx].assigned_id = id;
                    total_binned++;
                }
            }
        }
    }
    std::cout << "Successfully binned and assigned IDs to " << total_binned << " primary stars." << std::endl;

    // Assign companion IDs
    size_t companions_assigned = 0;
    for (size_t i = 0; i < all_stars.size(); i++)
    {
        StarRecord& r = all_stars[i];
        if (r.component > 0 && r.is_soles)
        {
            int host_idx = -1;
            if (!r.orig_host_id.empty() && orig_host_to_star_idx.find(r.orig_host_id) != orig_host_to_star_idx.end())
            {
                host_idx = orig_host_to_star_idx[r.orig_host_id];
            }
            else
            {
                // Fallback: look back up to 10 stars in soles_alienorum
                for (int k = (int)i - 1; k >= 0 && k >= (int)i - 10; k--)
                {
                    if (all_stars[k].is_soles && all_stars[k].component == 0)
                    {
                        host_idx = k;
                        break;
                    }
                }
            }

            if (host_idx >= 0 && !all_stars[host_idx].assigned_id.empty())
            {
                r.assigned_id = all_stars[host_idx].assigned_id + " " + std::string(1, r.component);
                companions_assigned++;
            }
        }
    }
    std::cout << "Assigned companion IDs to " << companions_assigned << " companion stars." << std::endl;

    // 9. Create catalogs/cross_ref directory and write 88 constellation files
    mkdir("catalogs/cross_ref", 0775);

    std::map<std::string, std::vector<StarRecord>> cons_records;
    for (const auto& r : all_stars)
    {
        if (r.vmag < 10.0 && !r.assigned_id.empty() && !r.cons.empty())
        {
            cons_records[r.cons].push_back(r);
        }
    }

    for (auto& [cons, recs] : cons_records)
    {
        std::sort(recs.begin(), recs.end(), [](const StarRecord& a, const StarRecord& b) {
            return a.vmag < b.vmag;
        });

        std::string filename = "catalogs/cross_ref/" + cons + ".dat";
        std::ofstream out(filename);
        if (!out.is_open()) continue;

        out << "#Alienorum_ID  Gaia_Source_ID      TYC            HIP     HD      Gliese           Bayer_Flamsteed      Gould  G_Cons Vmag    C\n";
        for (const auto& r : recs)
        {
            char buf[256];
            std::string gaia_s = (r.gaia_id > 0) ? std::to_string(r.gaia_id) : "";
            std::string hip_s = (r.hip > 0) ? std::to_string(r.hip) : "";
            std::string hd_s = (r.hd > 0) ? std::to_string(r.hd) : "";
            snprintf(buf, sizeof(buf), "%-15s%-20s%-15s%-8s%-8s%-17s%-21s%-7s%-7s%+7.2f %c",
                     r.assigned_id.c_str(),
                     gaia_s.c_str(),
                     r.tyc.c_str(),
                     hip_s.c_str(),
                     hd_s.c_str(),
                     r.gliese.c_str(),
                     r.bayer_flam.c_str(),
                     r.gould.c_str(),
                     r.gould_cons.c_str(),
                     r.vmag,
                     r.color);
            out << buf << "\n";
        }
        out.close();
    }
    std::cout << "Written cross-reference files for " << cons_records.size() << " constellations into catalogs/cross_ref/." << std::endl;

    // 10. Update soles_alienorum.dat
    size_t updated_soles_under10 = 0;
    size_t blanked_soles_over10 = 0;

    std::ofstream soles_out("catalogs/soles_alienorum.dat");
    if (!soles_out.is_open())
    {
        std::cerr << "Failed to open catalogs/soles_alienorum.dat for writing" << std::endl;
        return 1;
    }

    for (const auto& r : all_stars)
    {
        if (!r.is_soles || r.soles_index < 0) continue;
        std::string l = soles_lines[r.soles_index];

        std::string final_id = "";
        if (r.vmag < 10.0 && !r.assigned_id.empty())
        {
            final_id = r.assigned_id;
            updated_soles_under10++;
        }
        else
        {
            blanked_soles_over10++;
        }

        std::string id_col = final_id;
        if (id_col.size() < 14) id_col += std::string(14 - id_col.size(), ' ');
        else id_col = id_col.substr(0, 14);

        if (l.size() >= 14)
        {
            l.replace(0, 14, id_col);
        }
        soles_out << l << "\n";
    }
    soles_out.close();

    std::cout << "Updated soles_alienorum.dat: " << updated_soles_under10 << " stars with true Alienorum IDs, "
              << blanked_soles_over10 << " stars with omitted/blank IDs (V >= 10.0)." << std::endl;

    return 0;
}
