#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <math.h>
#include <fstream>
#include <ctime>

#include "misc.h"
using namespace alienorum;

// IMPORTANT: Any global variable defined here must also be extern declared in misc.h.
std::string loading_msg = "Loading...";
std::vector<std::string> themes;
std::string viewer_theme = "Perseus";
std::mutex mtx;
const char* vmtext[NUM_VIEWMODES] = { "Sky Atlas", "Horizon", "Sun Clock" };
ViewMode view_mode = vm_skyatlas;
int ncelobjs = 0;
int selected = -1, trackidx = -1;
double azimuth = 0, altitude = 0;
double spin = 0;
double global_gamma = 1.3;
double zoom = 1, vm, vmfr;
double viewer_lat, viewer_lon, viewer_home_lat, viewer_home_lon;
bool save_viewer_latlon = true;
bool show_grid = true, show_consln = true, show_xonsm = false, show_labels = true, show_orbits = false, draw_actual_conslines;
int cursor_size = 8, circle_size = 2, xaorngsim = 0;
int is_an_obj_under_cursor = -1;
double obj_magn_under_cursor;
std::string objname, objinfo;
bool is_mouse_over_window;
int objinfwnd_hei = 0;
int timeout_ms = 5;
bool dragging, dragged, viewchanged, randomize_txgen=true;
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
bool show_taucalc = false;
bool hide_mouse = true;
bool searched = false;
const char* lbltypes[nlbltyp] = { "Brightest", "Intrinsic", "Nearby", "Bayer", "Flamsteed",
    "Sunlike", "Has Planets", "Planet in HZ", "Binary Orbit", "Known Poles" };
const char* celtypes[nceltyp] = { "Galaxy", "Star", "Planet", "Moon", "Satellite" };
const char* compass[16] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
bool have_Gliese = false, have_BSC = false, have_HIP = false, have_WD = false, have_CCDM = false, have_SB9 = false,
    have_astorb = false, have_exo = false;
int cbolbls_selected_idx = 0, cboceltyp_selected_idx = 0, celidx_sel_in_sysxplor = 0;
double bv_correction = -0.62;
double sphere_quality = 1, npaz = 0, luminous_flux = 0;
bool lbl_localsys = true;
double lbllsys_mass_lim = 2.5e+23;
float has_water, ice_amount, veg_height, mtn_height;
int vegetation_r, vegetation_g, vegetation_b;

double appmagn_lblcut = 2.5,
       absmagn_lblcut = -3.5,
       distance_lblcut = 25*light_year;
char lblcut0[256], lblcut1[256], lblcut2[256];
int planets_lblcut = 1;

double intrinsic_cutoff = pow(magnbase, -6.5);
PerlinNoise pn;

const std::string WHITESPACE = " \n\r\t\f\v";
uint32_t xonsm[13] = {0x0e432843, 0x0e4328ec, 0x25443485, 0x29cc28ec, 0x29cc513a, 0x43363485, 0x511e0000, 0x511e3485, 0x511e513a, 0x511e5147, 0x511eab3a, 0x2b85e980, 0x57e47000};
double magnbase = pow(100, 1.0/5), invlogmagnbase = 1.0 / log(magnbase);
std::vector<std::string> consname, consabbrev, consgen;

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

int Grkno_from_abbrev(char *abbrev)
{
    int i;
    for (i=0; i<24; i++)
    {
        if (Greek_letter[i][0] == abbrev[0]
            && Greek_letter[i][1] == abbrev[1]
            && Greek_letter[i][2] == abbrev[2]
            )
            return i;
    }
    return -1;
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

bool download_file(std::string URL, std::string save_path)
{
    curlpp::init();

    try
    {
        std::string buffer;
        curlpp::Easy easy;

        easy.setOpt(CURLOPT_URL, URL.c_str());
        curlpp::List header{"User-Agent: Alienorum (https://github.com/electronicsbyjulie/alienorum)"};
        easy.setOpt(CURLOPT_HTTPHEADER, header.getHandle());
        easy.setOpt(CURLOPT_WRITEDATA, &buffer);
        easy.setOpt(CURLOPT_WRITEFUNCTION, curlpp::write::toString);

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
