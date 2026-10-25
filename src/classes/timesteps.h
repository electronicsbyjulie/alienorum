#ifndef ALIENORUM_TIME_STEPS_H
#define ALIENORUM_TIME_STEPS_H

#include <algorithm>
#include <cmath>
#include <limits>

namespace alienorum
{
    // Find the nearest same-solar-time epoch in the requested travel direction.
    template<class Phase>
    double solar_time_preserving_step(double original, double days, double period, const Phase &phase)
    {
        const double nominal = original + days;
        if (!std::isfinite(original) || !std::isfinite(days) || !std::isfinite(period)
            || !(period > 0) || days == 0) return nominal;
        constexpr double pi = 3.14159265358979323846;
        const double target = phase(original);
        if (!std::isfinite(target) || target < 0) return nominal;
        const double minimum_step = std::max(
            std::max(1e-8, fabs(original) * std::numeric_limits<double>::epsilon() * 8),
            std::min(fabs(days) * .001, period * .01));
        const auto error = [&](double epoch)
        {
            const double value = phase(epoch);
            return std::isfinite(value) && value >= 0
                ? std::remainder(value - target, 2 * pi) : std::numeric_limits<double>::quiet_NaN();
        };
        const auto in_direction = [&](double epoch)
        {
            return (epoch - original) * (days > 0 ? 1 : -1) > minimum_step;
        };
        double epoch = nominal;
        for (int iteration = 0; iteration < 16; ++iteration)
        {
            const double residual = error(epoch);
            if (!std::isfinite(residual)) return nominal;
            if (fabs(residual) < 1e-7 && in_direction(epoch)) return epoch;
            const double delta = std::max(1.0 / 86400, std::min(period * .001, 1.0 / 24));
            const double next = error(epoch + delta);
            const double rate = std::remainder(next - residual, 2 * pi) / delta;
            if (!std::isfinite(rate) || fabs(rate) < 1e-10) break;
            epoch -= std::clamp(residual / rate, -period * .45, period * .45);
            if (fabs(epoch - nominal) > period * 1.5 || !in_direction(epoch)) break;
        }
        // A slowly rotating or librating body can defeat Newton iteration.
        // Ignore phase-wrap discontinuities and bracket genuine crossings only.
        const double low = days > 0 ? std::max(original + minimum_step, nominal - period * 1.5) : nominal - period * 1.5;
        const double high = days < 0 ? std::min(original - minimum_step, nominal + period * 1.5) : nominal + period * 1.5;
        double best = nominal, best_distance = std::numeric_limits<double>::infinity();
        double left = low, left_error = error(left);
        for (int sample = 1; sample <= 96; ++sample)
        {
            const double right = low + (high - low) * sample / 96;
            const double right_error = error(right);
            if (std::isfinite(left_error) && std::isfinite(right_error)
                && left_error * right_error <= 0 && fabs(left_error - right_error) < pi)
            {
                double a = left, b = right, fa = left_error;
                for (int iteration = 0; iteration < 48; ++iteration)
                {
                    const double middle = a + (b - a) * .5, fm = error(middle);
                    if (!std::isfinite(fm)) break;
                    if (fabs(fm) < 1e-7) { a = b = middle; break; }
                    if (fa * fm <= 0) b = middle;
                    else { a = middle; fa = fm; }
                }
                const double candidate = a + (b - a) * .5;
                if (in_direction(candidate) && fabs(error(candidate)) < 1e-6 && fabs(candidate - nominal) < best_distance)
                {
                    best = candidate;
                    best_distance = fabs(candidate - nominal);
                }
            }
            left = right;
            left_error = right_error;
        }
        return best;
    }
}

#endif
