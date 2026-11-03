#pragma once

#include <array>
#include <algorithm>
#include <cmath>

namespace alienorum
{
    // Linear energy-balance response in emitted flux: C dT/dt = absorbed - emitted.
    // C contains cp*p/g and a surface mixed layer. Periodic history is evaluated directly,
    // so results are independent of frame rate and the order of simulation time jumps.
    // https://esd.copernicus.org/articles/11/1195/2020/
    struct ThermalOrbit
    {
        double eccentricity = 0, mean_anomaly = 0, period = 1;
        double pole_u = 0, pole_v = 0;

        double eccentric_anomaly(double mean) const
        {
            constexpr double pi = 3.14159265358979323846;
            mean = std::remainder(mean, 2 * pi);
            double low = -pi, high = pi, value = mean;
            for (int i = 0; i < 40; ++i)
            {
                const double residual = value - eccentricity * std::sin(value) - mean;
                if (std::abs(residual) < 1e-13) break;
                if (residual > 0) high = value;
                else low = value;
                const double next = value - residual / (1 - eccentricity * std::cos(value));
                value = next > low && next < high ? next : (low + high) * 0.5;
            }
            return value;
        }

        double declination_sine(double eccentric) const
        {
            const double x = std::cos(eccentric) - eccentricity;
            const double y = std::sqrt(1 - eccentricity * eccentricity) * std::sin(eccentric);
            return std::clamp((pole_u * x + pole_v * y) / (1 - eccentricity * std::cos(eccentric)), -1.0, 1.0);
        }
    };

    inline double daily_stellar_heating(double sin_latitude, double sin_declination)
    {
        constexpr double pi = 3.14159265358979323846;
        const double a = sin_latitude * sin_declination;
        const double b = std::sqrt(std::max(0.0, (1 - sin_latitude * sin_latitude) * (1 - sin_declination * sin_declination)));
        if (b < 1e-12) return std::max(0.0, a);
        const double sunset = std::acos(std::clamp(-a / b, -1.0, 1.0));
        return (a * sunset + b * std::sin(sunset)) / pi;
    }

    struct ThermalHistory
    {
        struct Sample { double weight = 0, flux = 0, sin_declination = 0; };
        std::array<Sample, 128> samples{};
        double global_flux = 0;

        ThermalHistory(const ThermalOrbit& orbit, double response_seconds)
        {
            constexpr double pi = 3.14159265358979323846;
            const double eccentric_now = orbit.eccentric_anomaly(orbit.mean_anomaly);
            const double mean_now = eccentric_now - orbit.eccentricity * std::sin(eccentric_now);
            const double omega = 2 * pi / orbit.period;
            const double tau = std::max(1e-6, response_seconds);
            const bool short_memory = tau < 0.05 * orbit.period;
            const double span = std::min(orbit.period, 10 * tau);
            double total = 0;
            for (size_t i = 0; i < samples.size(); ++i)
            {
                double eccentric, lag, duration;
                if (short_memory)
                {
                    lag = span * (i + 0.5) / samples.size();
                    eccentric = orbit.eccentric_anomaly(mean_now - omega * lag);
                    duration = span / samples.size();
                }
                else
                {
                    eccentric = eccentric_now - 2 * pi * (i + 0.5) / samples.size();
                    lag = (mean_now - eccentric + orbit.eccentricity * std::sin(eccentric)) / omega;
                    duration = orbit.period / samples.size() * (1 - orbit.eccentricity * std::cos(eccentric));
                }
                auto& sample = samples[i];
                sample.weight = duration * std::exp(-lag / tau);
                sample.flux = 1 / std::pow(1 - orbit.eccentricity * std::cos(eccentric), 2);
                sample.sin_declination = orbit.declination_sine(eccentric);
                total += sample.weight;
            }
            for (auto& sample : samples)
            {
                sample.weight /= total;
                global_flux += sample.weight * sample.flux;
            }
        }

        double local_flux(double sin_latitude, double annual_heating, double seasonal_share) const
        {
            double value = 0;
            for (const auto& sample : samples)
            {
                const double seasonal = annual_heating > 1e-9
                    ? daily_stellar_heating(sin_latitude, sample.sin_declination) / annual_heating : 1;
                value += sample.weight * sample.flux * (1 - seasonal_share + seasonal_share * seasonal);
            }
            return value;
        }
    };
}
