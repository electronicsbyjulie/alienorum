#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace alienorum
{
    struct SurfaceRefraction
    {
        double curvature = 0; // Quadratic height drop per squared metre.
        double range = 0, dip = 0, lift = 0, gradient = 0;

        static SurfaceRefraction from_lift(double radius, double eye, double lift)
        {
            SurfaceRefraction result;
            eye = std::max(.01, eye);
            radius = std::max(1.0, radius);
            const double geometric_dip = acos(radius / (radius + eye));
            result.dip = std::max(1e-8, geometric_dip - lift);
            const double slope = tan(result.dip);
            result.curvature = slope * slope / (4 * eye);
            result.range = 2 * eye / slope;
            result.lift = lift;
            return result;
        }

        static SurfaceRefraction prepare(double radius, double eye, double pressure, double temperature,
            double gravity, double molar_mass, double standard_refractivity, double supplied_gradient = NAN)
        {
            radius = std::max(1.0, radius);
            eye = std::max(.01, eye);
            const double nu = pressure > 0 && temperature > 0
                ? standard_refractivity * pressure / 101325 * 273.15 / temperature : 0;
            const auto smooth = [](double value)
            {
                value = std::clamp(value, 0.0, 1.0);
                return value * value * (3 - 2 * value);
            };
            // Estimated boundary-layer closure, calibrated against the Venera
            // range of 0.1--1 K/m. It is not a universal measured lapse rate.
            const double dense_hot = smooth((nu - .0005) / .0145) * smooth((temperature - 320) / 380);
            const double bulk = .0065 * std::max(0.0, gravity) / 9.81;
            const double gradient = std::isfinite(supplied_gradient) ? supplied_gradient
                : bulk * (1 - dense_hot) + .7 * dense_hot;
            const double ray_curvature = nu > 0 ? nu / (1 + nu) / temperature
                * (molar_mass * gravity / 8.314462618 - gradient) : 0;
            // A finite effective-radius approximation cannot represent atmospheric
            // ducts. Bound that singular limit at 100 geometric radii.
            const double curvature = .5 * std::max(.01 / radius, 1 / radius - ray_curvature);
            const double dip = atan(2 * sqrt(eye * curvature));
            auto result = from_lift(radius, eye, acos(radius / (radius + eye)) - dip);
            result.gradient = gradient;
            return result;
        }
    };
}
