#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <math.h>
#include <fstream>
#include <ctime>
#include <curl/curl.h>
#include "misc.h"
using namespace alienorum;

// IMPORTANT: Any global variable defined here must also be extern declared in misc.h.
bool done = false;
std::string loading_msg = "Loading...";
std::vector<std::string> themes;
std::string viewer_theme = "Perseus";
std::mutex mtx;
const char* vmtext[NUM_VIEWMODES] = { "Sky Atlas", "Horizon", "Sun Clock", "Sky Map" };
const char* vptext[NUM_VPLANES] = { "Local", "ICRF", "Ecliptic", "Galactic" };
ViewerPlaneMode vplane_mode = vplane_local;
ViewMode view_mode = vm_skyatlas;
int ncelobjs = 0;
int nsatobjs = 0;
int selected = -1, trackidx = -1;
double azimuth = 0, altitude = 0;
double spin = 0;
double global_gamma = default_gamma, viewer_gamma = global_gamma;
double zoom = 1, vm, vmfr;
double viewer_lat, viewer_lon, viewer_home_lat, viewer_home_lon;
double neighb_rthresh = 25 * light_year;
bool save_viewer_latlon = true;
bool show_grid = true, show_consln = true, show_xonsm = false, show_labels = true, show_orbits = false, show_sats = true, show_axes = false, draw_actual_conslines;
bool satview_upsidedown = false;
int cursor_size = 8, circle_size = 2, xaorngsim = 0;
int is_an_obj_under_cursor = -1;
double obj_magn_under_cursor;
std::string objname, objinfo, viewer_locale;
bool is_mouse_over_window;
int objinfwnd_hei = 0;
int timeout_ms = 5;
bool draggable, dragging, dragged, editing, viewchanged, randomize_txgen=true, updating_sats=false;
bool generating_fic_texture = false;
int lmx, lmy, whereami=0, iamhome=0, took_off_from=0, tookoff_countdown=0;
double velocmag;
double simnow = std::time(nullptr);
double JDnow = ((double)simnow - J2000_TIME_T)/oneday + J2000;
bool objinfwnd = true;
bool statuswnd = true;
bool objedtwnd = false;
bool satwnd = false;
bool astwnd = false;
bool addcelwnd = false;
bool explorer = false;
bool neighborhood = false;
bool locwnd = false;
bool show_taucalc = false;
bool hide_mouse = true;
bool label_galaxies = true;
bool show_galaxy_band = true;

// Set every frame by compute_object_draw_coordinates(): the cels[] index of the galaxy whose disc
// the viewer is standing inside, or -1. Only one galaxy can qualify, and normally it is ours.
int inside_galaxy_idx = -1;
bool searched = false, focus_findbox = false, whtbkgd = false;
double mag_limit_adjusted = normal_best_mag_limit;
const char* lbltypes[nlbltyp] = { "Brightest (A)", "Intrinsic (V)", "Nearby (Sh+N)", "Bayer (Sh+F)", "Flamsteed (F)", "Gould (Sh+G)",
    "Sunlike (Sh+C)", "Has Planets (Sh+P)", "Planet in HZ (Sh+L)", "Binary Orbit (2)", "Known Poles (Sh+X)" };
const char* celtypes[nceltyp] = { "Galaxy", "Star", "Planet", "Moon", "Satellite" };
const char* compass[16] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
bool have_Gliese = false, have_BSC = false, have_HIP = false, have_WD = false, have_CCDM = false, have_SB9 = false, have_Uranio = false,
    have_astorb = false, have_exo = false, have_RC3 = false, have_UNGC = false,
    noexo = false, nosats = false, radio_silence = false, keyprobe = false;
int cbolbls_selected_idx = lbltype_brightest, cboceltyp_selected_idx = 0, celidx_sel_in_sysxplor = 0, first_sat = -1;
double bv_correction = 0;
double sphere_quality = 1, npaz = 0, luminous_flux = 0, sclk_scale = 1;
bool lbl_localsys = true, show_localsys = true, mouse_over_menu = false, menu_clicked = false;
double lbllsys_mass_lim = 2.5e+23;
float has_water, veg_min_temp = 278, veg_max_temp = 310;
int vegetation_r, vegetation_g, vegetation_b;

double appmagn_lblcut = 2.5,
       absmagn_lblcut = -3.5,
       distance_lblcut = 25*light_year;
char lblcut0[256], lblcut1[256], lblcut2[256];
int planets_lblcut = 1, menu_ht = 21;

double intrinsic_cutoff = pow(magnbase, -normal_best_mag_limit);
PerlinNoise pn;

const std::string WHITESPACE = " \n\r\t\f\v";
uint32_t xonsm[13] = {0x0e432843, 0x0e4328ec, 0x25443485, 0x29cc28ec, 0x29cc513a, 0x43363485, 0x511e0000, 0x511e3485, 0x511e513a, 0x511e5147, 0x511eab3a, 0x2b85e980, 0x57e47000};
double magnbase = pow(100, 1.0/5), invlogmagnbase = 1.0 / log(magnbase);

std::string Greek_letter[24] =
{
    "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta",
    "Eta", "Theta", "Iota", "Kappa", "Lambda", "Mu",
    "Nu", "Xi", "Omicron", "Pi", "Rho", "Sigma",
    "Tau", "Upsilon", "Phi", "Chi", "Psi", "Omega"
};

double frand(double lmin, double lmax)
{
    int r = rand();
    double f = (double)r / RAND_MAX;
    f *= (lmax-lmin);
    return f+lmin;
}

std::string Greek_from_abbrev(char *abbrev)
{
    int i;
    for (i=0; i<24; i++)
    {
        if (Greek_letter[i][0] == abbrev[0]
            && Greek_letter[i][1] == abbrev[1]
            && Greek_letter[i][2] == abbrev[2]
            )
            return Greek_letter[i];
    }
    return std::string("");
}

double blackbody_flux(double T, double nu)
{
    double c = speed_of_light, h = Planck;

    double numerator = 2.0 * h * std::pow(c, 2);
    double exponent = (h * c) / (nu * kB * T);
    double denominator = std::pow(nu, 5) * (std::exp(exponent) - 1.0);

    return numerator / denominator;
}

double compute_time_dilation(double velocity)
{
    return sqrt(1.0 - (velocity*velocity)/(speed_of_light*speed_of_light));
}

// Solve Kepler's Equation: M = E - e*sin(E) using Newton's Method
double solve_Kepler(double M, double e)
{
    double E = M; // Initial guess
    double delta;
    do
    {
        delta = E - e * std::sin(E) - M;
        E = E - delta / (1.0 - e * std::cos(E));
    } while (std::abs(delta) > 1e-10);
    return E;
}

double lon_from_x(double x)
{
    return fmod(sclk_scale * x + azimuth, _pi*2);
}

double lat_from_y(double y)
{
    return altitude - sclk_scale * y;
}

void enforce_y_pan_limit()
{
    double limit = half_pi;
    if (view_mode == vm_skymap || view_mode == vm_sunclock)
    {
        limit = fmax(0, half_pi * (1.0 - 1.0 / zoom));
    }
    if (altitude >  limit) altitude =  limit;
    if (altitude < -limit) altitude = -limit;
}

std::string elapsed_time(time_t start, time_t end)
{
    int seconds = end-start;
    int minutes = seconds/60;
    seconds -= 60*minutes;
    int hours = minutes/60;
    minutes -= 60*hours;
    int days = hours/24;
    hours -= 24*days;

    std::string elapsed;
    if (days)
    {
        if (days < 10) elapsed += (std::string)"0";
        elapsed += std::to_string(days);
        elapsed += (std::string)":";
    }
    if (hours)
    {
        if (hours < 10) elapsed += (std::string)"0";
        elapsed += std::to_string(hours);
        elapsed += (std::string)":";
    }
    if (minutes < 10) elapsed += (std::string)"0";
    elapsed += std::to_string(minutes);
    elapsed += (std::string)":";
    if (seconds < 10) elapsed += (std::string)"0";
    elapsed += std::to_string(seconds);
    return elapsed;
}

std::string cons_from_alienorumid(const std::string alienorumid)
{
    std::string result = "";
    const char *c = alienorumid.c_str();
    if (!c) return result;
    const char *space = strchr(c, ' ');
    if (!space) return result;
    c = space+1;
    if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')) result += std::string(1, *c);
    c++;
    if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')) result += std::string(1, *c);
    c++;
    if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')) result += std::string(1, *c);
    return result;
}

int grkno_from_abbrev(const char *abbrev)
{
    static const int ionly1[26] = {0, 1, 21, 3, -1, -2, 2, -2, 8, -2, 9, 10, 11, 12, -1, -1, -2, 16, 17, -1, 19, -2, -2, 13, -2, 5};
    char c = abbrev[0] & 0x5f;
    int i = c - 'A', idx, result=-1;
    idx = ionly1[i];
    if (idx < -1) result = -1;
    else if (idx >= 0) result = idx;
    else if (!abbrev[1])
    {
        // TODO:
    }
    else if (!abbrev[2])
    {
        return -1;
    }
    else if (c == 'E')
    {
        c = abbrev[1] & 0x5f;
        if (c == 'P') result = 4;
        else if (c == 'T') result = 6;
    }
    else if (c == 'T')
    {
        c = abbrev[1] & 0x5f;
        if (c == 'H') result = 7;
        else if (c == 'A') result = 18;
    }
    else if (c == 'O')
    {
        c = abbrev[2] & 0x5f;
        if (c == 'I') result = 14;
        else if (c == 'E') result = 23;
    }
    else if (c == 'P')
    {
        c = abbrev[1] & 0x5f;
        if (c == 'I') result = 15;
        else if (c == 'H') result = 20;
        else if (c == 'S') result = 22;
    }

    if (result >= 0 && abbrev[3])
    {
        int n = abbrev[3] - '0';
        if (n > 0) result = 100 + 10*result + n;
    }

    return result;
}

// TODO: Consider storing the gases in a JSON file.
double atmospheric_tau(double normalized_pressure,
    double co2_fraction,
    double ch4_fraction,
    double h2o_fraction,
    double n2o_fraction,
    double o3_fraction,
    double so2_fraction,
    double h2s_fraction,
    double co_fraction,
    double hcn_fraction,
    double h2_fraction,
    double nh3_fraction,
    double c2h6_fraction)
{
    // Calculate partial pressures of greenhouse gases
    double p_co2 = normalized_pressure * co2_fraction;
    double p_ch4 = normalized_pressure * ch4_fraction;
    double p_h2o = normalized_pressure * h2o_fraction;
    double p_n2o = normalized_pressure * n2o_fraction; // Nitrous Oxide
    double p_o3  = normalized_pressure * o3_fraction;  // Ozone
    double p_so2 = normalized_pressure * so2_fraction; // Sulfur Dioxide
    double p_h2s = normalized_pressure * h2s_fraction; // Hydrogen Sulfide
    double p_co  = normalized_pressure * co_fraction;  // Carbon Monoxide
    double p_hcn = normalized_pressure * hcn_fraction; // Hydrogen Cyanide
    double p_nh3 = normalized_pressure * nh3_fraction;
    double p_h2  = normalized_pressure * h2_fraction;
    double p_c2h6 = normalized_pressure * c2h6_fraction;

    // 4. Dense Atmosphere Collision-Induced Absorption (CIA)
    double tau_cia = 0.0;
    if (normalized_pressure > 0.5) {
        tau_cia += 0.08 * (p_h2 * normalized_pressure); 
    }

    double h2_feedback = 1.0 + (p_h2 * 25.0);

    // Calculate Greenhouse Optical Depth (tau) using logarithmic scaling.
    double tau_co2 = 0.5  * std::log1p(p_co2 * 100.0);
    double tau_ch4 = 1.2  * std::log1p(p_ch4 * 500.0 * h2_feedback);
    double tau_h2o = 0.8  * std::log1p(p_h2o * 50.0);
    double tau_n2o = 0.7  * std::log1p(p_n2o * 350.0); // Potent, severe saturation curve
    double tau_o3  = 0.4  * std::log1p(p_o3  * 150.0); // Moderate greenhouse agent
    double tau_so2 = 0.5  * std::log1p(p_so2 * 80.0);  // Volcanic greenhouse component
    double tau_h2s = 0.15 * std::log1p(p_h2s * 20.0);  // Volcanic greenhouse component
    double tau_co  = 0.05 * std::log1p(p_co  * 10.0);  // Extremely weak direct thermal trapping
    double tau_hcn = 0.1  * std::log1p(p_hcn * 30.0);  // Minor primordial trace gas
    double tau_nh3 = 1.7  * std::log1p(p_nh3 * 800.0);
    double tau_c2h6 = 0.20 * std::log1p(p_c2h6 * 600.0);

    return fmax(0, tau_co2 + tau_ch4 + tau_h2o + tau_n2o + tau_o3 + tau_so2 + tau_h2s + tau_co + tau_hcn + tau_nh3 + tau_cia - tau_c2h6);
}

long long micronow()
{
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return duration.count();
}

// Trim from start (left)
std::string ltrim(const std::string &s)
{
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

// Trim from end (right)
std::string rtrim(const std::string &s)
{
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

// Trim from both ends
std::string trim(const std::string &s)
{
    return rtrim(ltrim(s));
}

int Damerau_Levenshtein(const std::string& s1, const std::string& s2)
{
    const std::size_t m = s1.size();
    const std::size_t n = s2.size();

    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    for (std::size_t i = 0; i <= m; ++i) dp[i][0] = i;
    for (std::size_t j = 0; j <= n; ++j) dp[0][j] = j;

    for (std::size_t i = 1; i <= m; ++i)
    {
        for (std::size_t j = 1; j <= n; ++j)
        {
            char c1 = s1[i - 1];
            char c2 = s2[j - 1];
            if (c1 >= 'A' && c1 <= 'Z') c1 += 0x20;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 0x20;
            int cost = (c1 == c2) ? 0 : 1;

            dp[i][j] = std::min(
            {
                dp[i - 1][j] + 1,       // Deletion
                dp[i][j - 1] + 1,       // Insertion
                dp[i - 1][j - 1] + cost // Substitution
            });

            // Add the transposition check (Damerau modification)
            if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1])
            {
                dp[i][j] = std::min(dp[i][j], dp[i - 2][j - 2] + cost); // Transposition
            }
        }
    }
    return dp[m][n];
}

bool is_digit_or_dot(char c)
{
    return ((c >= '0' && c <= '9') || c == '.');
}

bool contains_digits_or_dots(const char *s)
{
    int i;
    for (i=0; s[i]; i++) if (is_digit_or_dot(s[i])) return true;
    return false;
}

bool has_same_numbers(const char *s1, const char *s2)
{
    int i, j, m, n;
    m = strlen(s1);
    n = strlen(s2);

    j=0;
    for (i=0; i<m; i++)
    {
        if (is_digit_or_dot(s1[i]))
        {
            while (j<n && !is_digit_or_dot(s2[j])) j++;
            if (s2[j] != s1[i]) return false;
            j++;
        }
    }

    for (; j<n; j++) if (is_digit_or_dot(s2[j])) return false;

    return true;
}

std::string lop_component(const char *name)
{
    std::string result = name;
    int n = strlen(name);
    if (name[n-1] >= 'A' && name[n-1] <= 'Z' && (name[n-2] == ' ' || (name[n-2] >= '0' && name[n-2] <= '9'))) result = trim(result.substr(0, n-1));
    return result;
}

bool file_exists(const char *fname)
{
    FILE *fp = fopen(fname, "r");
    if (fp)
    {
        fclose(fp);
        return true;
    }
    return false;
}

std::time_t file_age(const char *fname)
{
    std::filesystem::file_time_type ft = std::filesystem::last_write_time(fname);
    auto system_tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t mt = std::chrono::system_clock::to_time_t(system_tp);
    std::time_t now = std::time(nullptr);
    return now - mt;
}

bool download_file(std::string URL, std::string save_path)
{
    if (radio_silence) return false;
    curlpp::init();

    try
    {
        std::string buffer;
        curlpp::Easy easy;
        curlpp::List header{"User-Agent: Alienorum (https://github.com/electronicsbyjulie/alienorum)"};

        if (!strcmp(URL.substr(0, 6).c_str(), "ftp://"))
        {
            // Set target URL using the correct 2-argument signature
            easy.setOpt(CURLOPT_URL, URL.c_str());

            // Credentials and configuration flags
            easy.setOpt(CURLOPT_USERNAME, "anonymous");
            easy.setOpt(CURLOPT_PASSWORD, "[email protected]");

            // Crucial: Re-enable passive mode to get past firewalls
            curl_easy_setopt(easy.getHandle(), static_cast<CURLoption>(10085), 1L);           // CURLOPT_FTP_USE_PASV

            // Write directly into your string buffer using basic functional callbacks
            easy.setOpt(CURLOPT_WRITEDATA, &buffer);
            easy.setOpt(CURLOPT_WRITEFUNCTION, curlpp::write::toString);
        }
        else
        {
            easy.setOpt(CURLOPT_URL, URL.c_str());
            easy.setOpt(CURLOPT_HTTPHEADER, header.getHandle());
            easy.setOpt(CURLOPT_WRITEDATA, &buffer);
            easy.setOpt(CURLOPT_WRITEFUNCTION, curlpp::write::toString);
        }

        easy.perform();

        std::fstream fs(save_path.c_str(), std::ios::out | std::ios::binary);
        if (!fs)
        {
            curlpp::cleanup();
            std::cerr << "FAILED to write " << save_path << std::endl << std::flush;
            return false;
        }

        fs << buffer;
        fs.close();
    }
    catch (const curlpp::Exception& e)
    {
        std::cerr << "FAILED to download " << URL << ": " << e.what() << std::endl << std::flush;
        return false;
    }

    curlpp::cleanup();
    return true;
}

std::vector<std::string> parse_csv_row(const char *data)
{
    int n = strlen(data);
    if (!n) return std::vector<std::string>();

    char buffer[n+1];
    strcpy(buffer, data);
    char *cursor = buffer, *comma;
    std::vector<std::string> result;

    do
    {
        comma = strchr(cursor, ',');
        if (comma) *comma = 0;
        std::string value = cursor;
        result.push_back(value);
        cursor = comma+1;
    } while (comma);

    return result;
}

time_t from_iso_string(std::string iso_string, const char* format)
{
    std::istringstream iss(iso_string);
    std::tm tm_struct = {};

    iss >> std::get_time(&tm_struct, format ? format : "%Y-%m-%dT%H:%M:%S");

    if (iss.fail())
    {
        std::cerr << "FAILED to parse datetime " << iso_string << std::endl;
        throw 0xbad7177e;                       // Access to satellite data depends on this working. If we fail, we must exit the app or risk an IP ban.
    }
    else
    {
#ifdef _WIN32
        return _mkgmtime(&tm_struct); // Windows SDK
#else
        return timegm(&tm_struct);    // Linux/macOS POSIX
#endif
    }

    return time_t();
}

// Fractional Brownian Motion helper
double fBm(double x, double y, double z, int octaves, double lacunarity, double gain)
{
    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double maxValue = 0.0;  // Used for normalizing
    for (int i = 0; i < octaves; i++)
    {
        total += pn.noise(x * frequency, y * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    // Normalize to [-1, 1] then shift to [0, 1]
    return (total / maxValue + 1.0) / 2.0;
}

// Ridged multifractal: folds each octave around zero, so the result has sharp,
// high-contrast ridges and creases instead of fBm's smooth rolling hills --
// suited to rough, weathered-looking surface texture rather than clouds.
double ridged_fBm(double x, double y, double z, int octaves, double lacunarity, double gain)
{
    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double maxValue = 0.0;
    for (int i = 0; i < octaves; i++)
    {
        double n = 1.0 - fabs(pn.noise(x * frequency, y * frequency, z * frequency));
        total += n * n * amplitude;
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

double probability_density_function(double x, double mean, double std_dev)
{
    double exponent = -0.5 * std::pow((x - mean) / std_dev, 2);
    return (1.0 / (std_dev * std::sqrt(2.0 * _pi))) * std::exp(exponent);
}

int sgn(double f)
{
    if (f < 0) return -1;
    else if (f > 0) return 1;
    else return 0;
}

std::string Roman(int num)
{
    const std::vector<std::pair<int, std::string>> romanMapping =
    {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
        {100, "C"},  {90, "XC"},  {50, "L"},  {40, "XL"},
        {10, "X"},   {9, "IX"},   {5, "V"},   {4, "IV"},
        {1, "I"}
    };

    std::string result = "";

    // Loop through the map and reduce the number greedily
    for (const auto& [value, symbol] : romanMapping)
    {
        while (num >= value)
        {
            result += symbol;
            num -= value;
        }
    }

    return result;
}