#include "shore.h"

using namespace alienorum;

double CosmicShore::calculate_escape_velocity(const Planet& p)
{
    double massg = p.mass;
    double radiusM = p.volumetric_mean_radius * earth_radius;
    return std::sqrt((2.0 * G * massg) / radiusM);
}

#if 0
// Calculate Saturation Time (years) based on Spectral Type
// Approximation: F/G/K ~ 100 Myr, M ~ 1-7 Gyr depending on mass
double CosmicShore::get_saturation_time(const Star& s)
{
    const char *stype = s.spectral_type;
    while (*stype && !strchr("OBAFGKM", *stype)) stype++;

    if (*stype == 'F' || *stype == 'G' || *stype == 'K')
    {
        return 1.0e+8; // 100 Million years
    }
    else if (*stype == 'M')
    {
        // Simple scaling: lower mass M dwarfs saturate longer
        // Range: 1 Gyr (M0) to 7+ Gyr (M8)
        return 1.0e+9 * (1.0 / std::pow(s.mass / solar_mass, 2.5)); 
    }
    return 1.0e+8; // Default
}

// Calculate Saturation Fraction (L_XUV / L_bol)
// F/G/K ~ 10^-4, M ~ 10^-3
double CosmicShore::get_saturation_fraction(const Star& s)
{
    const char *stype = s.spectral_type;
    while (*stype && !strchr("OBAFGKM", *stype)) stype++;

    if (*stype == 'F' || *stype == 'G' || *stype == 'K')
    {
        return 1.0e-4; 
    }
    else if (*stype == 'M')
    {
        return 1.0e-3; 
    }
    return 1.0e-4;
}

// TOMAYBEDO: If add classic approach, clean up the formatting on these functions.

// Classic Approach (Requires explicit I_XUV calculation)
// Calculates cumulative XUV instellation (J/m^2) over star's lifetime
static double calculateCumulativeXUV(const Star& s, const Planet& p)
{
    double t_sat = getSaturationTime(s);
    double f_sat = getSaturationFraction(s);
    double l_bol_watts = s.luminosity * L_SOLAR;
    double distance_m = p.semimajorAxis * AU;
    
    // Flux at planet (W/m^2) during saturation
    double flux_sat = (f_sat * l_bol_watts) / (4.0 * M_PI * std::pow(distance_m, 2));
    
    // Convert age to seconds
    double age_sec = s.age * 365.25 * 24.0 * 3600.0;
    double t_sat_sec = t_sat * 365.25 * 24.0 * 3600.0;
    
    double energy_sat = flux_sat * std::min(age_sec, t_sat_sec);
    double energy_post_sat = 0.0;

    if (s.age > t_sat)
    {
        // Post-saturation decay: L_XUV ~ t^-1.2 (approx)
        // Integral of t^-beta from t_sat to t_age
        double beta = 1.2; 
        double term1 = std::pow(t_sat, 1.0 - beta);
        double term2 = std::pow(s.age, 1.0 - beta);
        
        // Normalization factor to ensure continuity at t_sat
        double integral_factor = (term1 - term2) / (beta - 1.0);
        // Average flux scaling relative to saturation flux
        // F(t) = F_sat * (t/t_sat)^-beta
        // Integral = F_sat * t_sat^beta * [t^(1-beta)/(1-beta)]
        double post_sat_flux_avg = flux_sat * std::pow(t_sat, beta) * (term1 - term2) / (beta - 1.0) / (s.age - t_sat);
        
        energy_post_sat = post_sat_flux_avg * (age_sec - t_sat_sec);
    }

    return energy_sat + energy_post_sat;
}

// Calculate ARM (Atmospheric Retention Metric)
// ARM > 0 implies retention, ARM < 0 implies loss
// Based on: 4*log10(v_esc) - log10(I_XUV) - C
static double calculateARM_Classic(double v_esc, double i_xuv_joules_m2)
{
    // Convert I_XUV to erg/cm^2 for standard comparison (1 J/m^2 = 1000 erg/cm^2)
    double i_xuv_erg = i_xuv_joules_m2 * 1000.0;
    
    // Calibrated constant C ~ 3.16 (for I in erg/cm^2 and v in km/s? Check units)
    // Using standard Zahnle & Catling form: log10(I) = 4*log10(v) + D
    // Let's use dimensionless log comparison relative to Earth
    // Earth: v_esc ~ 11.2 km/s, I_XUV ~ 2e18 erg/cm^2 (approx cumulative)
    
    double v_esc_km = v_esc / 1000.0;
    double log_i = std::log10(i_xuv_erg);
    double log_v = std::log10(v_esc_km);
    
    // Metric: Distance from the line log(I) = 4*log(v) + C_earth
    // C_earth approx: log10(2e18) - 4*log10(11.2) = 18.3 - 4.2 = 14.1
    double shoreline_val = 4.0 * log_v + 14.1;
    
    return shoreline_val - log_i; // Positive = Safe (below shoreline flux)
}

#endif

// Unified 3D Empirical Approach (No explicit XUV history required)
// Returns probability score or distance from boundary
// Equation: log10(f) = p*log10(v_esc) + q*log10(L_star) + C
// Based on recent 2025/2026 fits: p~6.0, q~1.2
double CosmicShore::calculate_unified_metric(Star& s, Planet& p)
{
    double v_esc_km = calculate_escape_velocity(p);

    // Bolometric flux at planet relative to Earth
    double flux = p.est_bolometric_flux();

    // Coefficients from recent 3D shoreline studies (e.g., 2025/2026 papers)
    double p_slope = 5.9; 
    double q_slope = 1.17;
    double c_intercept = -1.5; // Approximate calibration constant

    double lhs = std::log10(flux);
    double rhs = p_slope * std::log10(v_esc_km) + q_slope * std::log10(s.m_bol) + c_intercept;

    // Metric: Positive means "above" the shoreline (likely has atmosphere)
    // Note: Direction depends on exact definition of f (loss driver vs retention)
    // Usually higher flux = loss. So if LHS > RHS, it's in the "loss" zone.
    // We return RHS - LHS. Positive = atmosphere retention likely.
    return rhs - lhs;
}