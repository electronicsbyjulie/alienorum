#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <math.h>

#include "misc.h"

std::string loading_msg = "Loading...";
std::mutex mtx;
int ncelobjs = 0;
int selected = -1, trackidx = -1;
double azimuth = 0, altitude = 0;
double spin = 0;
double global_gamma = 1.3;
double zoom = 1, vm, vmfr;
bool show_grid = true, show_consln = true, show_xonsm = false, show_labels = true, show_orbits = false, draw_actual_conslines;
int cursor_size = 8, circle_size = 2.6, xaorngsim = 0;
ImU32 cursor_color = IM_COL32(255, 32, 0, 255);
ImU32 cursor_color1 = IM_COL32(96, 0, 24, 76);
ImU32 cursor_color2 = IM_COL32(160, 20, 20, 76);
ImU32 cursor_color3 = IM_COL32(255, 48, 0, 76);
ImU32 grid_color = IM_COL32(255, 0, 0, 96);
ImU32 grid_color_brighter = IM_COL32(255, 0, 0, 140);
ImU32 ecliptic_color = IM_COL32(0, 192, 255, 96);
ImU32 consline_color = IM_COL32(64, 64, 255, 128);
ImU32 conslbl_color = IM_COL32(255, 192, 0, 128);
ImU32 selected_color = IM_COL32(0, 255, 96, 192);
ImU32 selected_orbit_color = IM_COL32(0, 255, 96, 96);
ImU32 objlbl_color = IM_COL32(64, 255, 0, 176);
int is_an_obj_under_cursor = -1;
double obj_magn_under_cursor;
std::string objname, objinfo;
bool is_mouse_over_window;
int objinfwnd_hei = 0;
int timeout_ms = 5;
bool dragging, dragged, viewchanged;
int lmx, lmy, whereami=0, iamhome=0;
double velocmag;
double simnow = std::time(nullptr);
double JDnow = ((double)simnow - J2000_TIME_T)/oneday + J2000;
bool objinfwnd = true;
bool statuswnd = true;
bool objedtwnd = false;
bool addcelwnd = false;
bool hide_mouse = true;
bool searched = false;
const char* lbltypes[nlbltyp] = { "Brightest", "Intrinsic", "Nearby", "Sunlike", "Has Planets", "Binary Orbit", "Known Poles" };
const char* celtypes[nceltyp] = { "Galaxy", "Star", "Planet", "Moon", "Satellite" };
int cbolbls_selected_idx = 0, cboceltyp_selected_idx = 0;
double bv_correction = 0;

double appmagn_lblcut = 2.5,
       absmagn_lblcut = -3.5,
       distance_lblcut = 25*light_year;
char lblcut0[256], lblcut1[256], lblcut2[256];

double intrinsic_cutoff = pow(magnbase, -6.5);

const std::string WHITESPACE = " \n\r\t\f\v";
__uint32_t xonsm[13] = {0x0e432843, 0x0e4328ec, 0x25443485, 0x29cc28ec, 0x29cc513a, 0x43363485, 0x511e0000, 0x511e3485, 0x511e513a, 0x511e5147, 0x511eab3a, 0x2b85e980, 0x57e47000};
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

std::string Greek_from_abbrev(std::string abbrev)
{
    return Greek_from_abbrev(abbrev.c_str());
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

    for (int i = 0; i <= m; ++i) dp[i][0] = i;
    for (int j = 0; j <= n; ++j) dp[0][j] = j;

    for (int i = 1; i <= m; ++i)
    {
        for (int j = 1; j <= n; ++j)
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
