#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include "salinity.h"
#include "thermalclimate.h"

namespace alienorum
{
    struct IceForcing
    {
        double shortwave = 0, outgoing_at_freezing = 0, feedback = 1;
        double transported = 0, ocean_heat = 0;
    };

    struct IceState
    {
        double energy = 0, temperature = 273.15, thickness = 0;
        double frozen_mass_fraction = 0, cover = 0, absorbed = 0, net_flux = 0;
    };

    // Zero-layer conductive ice and a sensible-heat ocean reservoir. Negative
    // enthalpy stores latent heat, rather than replacing melting with a temperature
    // threshold. Method: Eisenman & Wettlaufer (2009), https://arxiv.org/abs/0812.4777
    // Depth, cloud masking, and albedos are scenario parameters, not measurements.
    struct IceColumn
    {
        double depth = 50, mixed_layer = 50, salinity = 35, pressure = 101325;
        double atmospheric_capacity = 0, ice_albedo = .6, water_albedo = .07, albedo_visibility = .5;
        double basal_flux = 0, conductivity = 2.2;
        double albedo_thickness = .5;
        double ocean_heat_share = .1;
        static constexpr double latent_per_kg = 334000, water_density = 1000, ice_density = 917;

        double latent() const { return water_density * depth * latent_per_kg; }
        double water_capacity() const { return atmospheric_capacity + 4.18e6 * std::min(depth, mixed_layer); }
        double solid_capacity() const { return atmospheric_capacity + 1.93e6 * std::min(2.0, depth * water_density / ice_density); }
        double freezing(double frozen = 0) const
        {
            const double ratio = salinity / std::max(1.0, 1000 - salinity);
            const double brine = salinity > 0 ? std::min(264.0, 1000 * ratio / (std::max(.0001, 1 - frozen) + ratio)) : 0;
            return seawater_freezing_k(brine, pressure);
        }

        IceState state(double energy, const IceForcing& forcing) const
        {
            IceState result;
            result.energy = energy;
            const double fraction = std::clamp(-energy / latent(), 0.0, 1.0);
            result.frozen_mass_fraction = fraction;
            result.thickness = fraction * depth * water_density / ice_density;
            result.cover = -std::expm1(-result.thickness / .1);
            double temperature = energy >= 0 ? freezing() + energy / water_capacity()
                : energy <= -latent() ? freezing(1) + (energy + latent()) / solid_capacity() : freezing(fraction);
            // Optical whiteness is not the areal ice/open-water albedo transition.
            // EW09 uses a 0.5 m transition scale; a 0.1 m optical cutoff makes the
            // climatic feedback abrupt enough to erase seasonal open-water states.
            const double climatic_cover = std::tanh(result.thickness / albedo_thickness);
            const double reflection = albedo_visibility * climatic_cover * (ice_albedo - water_albedo) / (1 - water_albedo);
            // Albedo reflects sunlight, not heat carried in from other regions.
            // Ocean transport heats the ice base. As the finite water column
            // freezes completely, return that allocation to atmospheric transport.
            // This changes heat placement, never the total incoming energy.
            const double bottom_heat = forcing.ocean_heat * (1 - fraction);
            result.absorbed = forcing.shortwave * (1 - reflection) + forcing.transported
                + forcing.ocean_heat - bottom_heat;
            // Floating ice loses heat through a thickness-dependent conductive
            // resistance. A melting surface stays at the local brine freezing point.
            if (energy < 0 && energy > -latent())
            {
                const double at_interface = result.absorbed - forcing.outgoing_at_freezing
                    - forcing.feedback * (temperature - freezing());
                temperature += std::min(0.0, at_interface / (forcing.feedback + conductivity / std::max(1e-9, result.thickness)));
            }
            result.temperature = std::max(2.7, temperature);
            result.net_flux = result.absorbed - forcing.outgoing_at_freezing
                - forcing.feedback * (result.temperature - freezing()) + basal_flux + bottom_heat;
            return result;
        }

        double initial_energy(double temperature) const
        {
            if (temperature >= freezing()) return (temperature - freezing()) * water_capacity();
            // A cold initial branch is an initial condition, not instant freezing
            // during a later warm interval. The orbital integration supplies history.
            return -ice_density * std::min(2.0, depth * water_density / ice_density) * latent_per_kg;
        }

        double advance(double energy, const IceForcing& forcing, double seconds) const
        {
            if (!(seconds > 0)) return energy;
            if (energy >= 0)
            {
                const double next = (energy + seconds * state(0, forcing).net_flux)
                    / (1 + seconds * forcing.feedback / water_capacity());
                if (next >= 0) return next;
            }
            if (energy <= -latent())
            {
                const double next = (energy + latent() + seconds * state(-latent(), forcing).net_flux)
                    / (1 + seconds * forcing.feedback / solid_capacity());
                if (next <= 0) return next - latent();
            }
            // Backward Euler enforces E_new-E_old=dt*net(E_new), including a step
            // that crosses freezing or exhausts the ice. Bracketing avoids explicit
            // time-step instability for long exoplanet years and thin ice layers.
            const double rate = state(energy, forcing).net_flux;
            double low = energy - std::max(1.0, std::abs(rate) * seconds + latent());
            double high = energy + std::max(1.0, std::abs(rate) * seconds + latent());
            const auto residual = [&](double value) { return value - energy - seconds * state(value, forcing).net_flux; };
            for (int i = 0; i < 32 && residual(low) > 0; ++i) low = energy - 2 * (energy - low);
            for (int i = 0; i < 32 && residual(high) < 0; ++i) high = energy + 2 * (high - energy);
            for (int i = 0; i < 48; ++i)
            {
                const double middle = (low + high) * .5;
                if (residual(middle) > 0) high = middle;
                else low = middle;
            }
            return (low + high) * .5;
        }
    };

    struct IceSeason
    {
        static constexpr size_t rows = 17, phases = 128;
        static constexpr double pi = 3.14159265358979323846;
        static constexpr const char* checkpoint_model = "conductive_enthalpy_v4";
        struct Curve
        {
            double baseline = 0, closure = 0;
            std::array<double, phases + 1> energy{};
        };
        IceColumn column;
        ThermalOrbit orbit;
        double absorbed_reference = 200, feedback = 3, redistribution = .9, reference = 1, regional_k = 0;
        double mean_k = 288, contrast_k = 0;
        bool seasonal = true, locked = false;
        double internal_flux_fraction = 0;
        size_t columns = 1;
        std::vector<std::vector<Curve>> histories;
        std::vector<double> annual_heating;
        double closure_relative = 0, annual_liquid_fraction = 0, maximum_thickness = 0;
        bool converged = true;

        double baseline_at(double latitude, double longitude) const
        {
            if (!locked) return mean_k + contrast_k * (1.0 / 3.0 - latitude * latitude);
            const double illumination = std::max(0.0, std::sqrt(std::max(0.0, 1 - latitude * latitude)) * std::cos(longitude));
            return mean_k * std::pow(internal_flux_fraction + (1 - internal_flux_fraction)
                * (redistribution + (1 - redistribution) * 4 * illumination), .25);
        }

        IceForcing forcing(size_t row, size_t longitude, double eccentric, double baseline = NAN) const
        {
            const double latitude = -1 + 2.0 * row / (rows - 1);
            const double angle = 2 * pi * longitude / columns;
            const double declination = orbit.declination_sine(eccentric);
            double direct = 1, transported = 0;
            if (locked)
            {
                const double illumination = std::max(0.0, latitude * declination
                    + std::sqrt(std::max(0.0, (1 - latitude * latitude) * (1 - declination * declination))) * std::cos(angle));
                direct = (1 - redistribution) * 4 * illumination;
                transported = redistribution;
            }
            else if (seasonal)
            {
                const double geometric = annual_heating[row] > 1e-9
                    ? daily_stellar_heating(latitude, declination) / annual_heating[row] : 1;
                const double local = 1 - .8 * redistribution;
                direct = local * geometric;
                transported = 1 - local;
            }
            const double flux = seasonal ? reference / std::pow(1 - orbit.eccentricity * std::cos(eccentric), 2) : 1;
            if (!std::isfinite(baseline)) baseline = baseline_at(latitude, angle);
            const double baseline_share = locked ? redistribution + (1 - redistribution) * 4
                * std::max(0.0, std::sqrt(std::max(0.0, 1 - latitude * latitude)) * std::cos(angle)) : 1;
            return {absorbed_reference * direct * flux,
                absorbed_reference * baseline_share + column.basal_flux
                    + feedback * (column.freezing() - baseline), feedback,
                absorbed_reference * transported * flux * (1 - column.ocean_heat_share),
                absorbed_reference * transported * flux * column.ocean_heat_share};
        }

        Curve integrate(size_t row, size_t longitude, double baseline) const
        {
            Curve curve;
            curve.baseline = baseline;
            double energy = column.initial_energy(baseline);
            if (!seasonal)
            {
                const auto steady = forcing(row, longitude, 0, baseline);
                for (int year = 0; year < 24; ++year) energy = column.advance(energy, steady, 31557600);
                curve.energy.fill(energy);
                return curve;
            }
            for (int year = 0; year < 24; ++year)
            {
                const double start = energy;
                curve.energy[0] = energy;
                for (size_t phase = 0; phase < phases; ++phase)
                {
                    const double begin = 2 * pi * phase / phases, end = 2 * pi * (phase + 1) / phases;
                    const double seconds = orbit.period / (2 * pi) * (end - begin - orbit.eccentricity * (std::sin(end) - std::sin(begin)));
                    energy = column.advance(energy, forcing(row, longitude, (begin + end) * .5, baseline), seconds);
                    curve.energy[phase + 1] = energy;
                }
                curve.closure = std::abs(energy - start) / std::max(column.latent(), column.water_capacity());
                if (curve.closure < 1e-7) break;
            }
            return curve;
        }

        IceSeason(const IceColumn& physical, const ThermalOrbit& annual, double mean, double contrast,
            double absorbed, double slope, double transport, double flux_reference, bool annual_cycle, bool synchronous,
            double regional_amplitude = 0, double internal_fraction = 0)
            : column(physical), orbit(annual), absorbed_reference(absorbed), feedback(slope), redistribution(transport),
              reference(flux_reference), regional_k(std::abs(regional_amplitude)), mean_k(mean), contrast_k(contrast),
              seasonal(annual_cycle), locked(synchronous), internal_flux_fraction(internal_fraction), columns(synchronous ? 8 : 1)
        {
            histories.resize(rows * columns);
            annual_heating.resize(rows);
            for (size_t row = 0; row < rows; ++row)
            {
                const double latitude = -1 + 2.0 * row / (rows - 1);
                for (size_t phase = 0; phase < phases; ++phase)
                    annual_heating[row] += daily_stellar_heating(latitude, orbit.pole_u * std::cos(2 * pi * phase / phases)
                        + orbit.pole_v * std::sin(2 * pi * phase / phases)) / phases;
                for (size_t longitude = 0; longitude < columns; ++longitude)
                {
                    const double angle = 2 * pi * longitude / columns;
                    const double baseline = baseline_at(latitude, angle);
                    auto& curves = histories[row * columns + longitude];
                    const auto central = integrate(row, longitude, baseline);
                    curves.push_back(central);
                    double cold = baseline, warm = baseline;
                    // Each geographic corner covers the actual baseline temperature
                    // of adjacent points. Latitude interpolation must not mix a warm
                    // ocean with a thick perennial ice column in place of solving the
                    // intermediate climate, which may freeze and thaw every year.
                    for (double z : {std::max(-1.0, latitude - 2.0 / (rows - 1)), latitude,
                        std::min(1.0, latitude + 2.0 / (rows - 1))})
                    for (int offset : {-1, 0, 1})
                    {
                        const double value = baseline_at(z, angle + (locked ? offset * 2 * pi / columns : 0));
                        cold = std::min(cold, value); warm = std::max(warm, value);
                    }
                    cold -= regional_k; warm += regional_k;
                    if (cold < baseline) curves.push_back(integrate(row, longitude, cold));
                    if (warm > baseline) curves.push_back(integrate(row, longitude, warm));
                    std::sort(curves.begin(), curves.end(), [](const auto& a, const auto& b) { return a.baseline < b.baseline; });
                    for (size_t index = 0; index + 1 < curves.size();)
                    {
                        const auto& a = curves[index];
                        const auto& b = curves[index + 1];
                        bool same_phase = true;
                        for (size_t phase = 0; phase <= phases; ++phase)
                            if ((a.energy[phase] >= 0) != (b.energy[phase] >= 0)) { same_phase = false; break; }
                        // Resolve ice/open-water transitions to 0.1 K, spending no
                        // extra integrations in uniformly warm or frozen intervals.
                        if (!same_phase && b.baseline - a.baseline > .1)
                        {
                            auto middle = integrate(row, longitude, (a.baseline + b.baseline) * .5);
                            curves.insert(curves.begin() + index + 1, std::move(middle));
                        }
                        else ++index;
                    }
                    for (const auto& curve : curves)
                    {
                        closure_relative = std::max(closure_relative, curve.closure);
                        converged = converged && curve.closure < 1e-7;
                    }
                    for (size_t phase = 0; phase < phases; ++phase)
                    {
                        const auto state = column.state(central.energy[phase], forcing(row, longitude, 2 * pi * phase / phases));
                        const double weight = (1 - orbit.eccentricity * std::cos(2 * pi * (phase + .5) / phases)) / phases;
                        const double area = (row == 0 || row + 1 == rows ? .5 : 1) / ((rows - 1) * double(columns));
                        annual_liquid_fraction += (state.thickness < .001 ? weight : 0) * area;
                        maximum_thickness = std::max(maximum_thickness, state.thickness);
                    }
                }
            }
        }

        double energy_at(double latitude, double longitude, double mean_anomaly, double regional_offset = 0) const
        {
            const double baseline = baseline_at(latitude, longitude) + regional_offset;
            double eccentric = orbit.eccentric_anomaly(mean_anomaly);
            if (eccentric < 0) eccentric += 2 * pi;
            const double phase = eccentric * phases / (2 * pi);
            const size_t low_phase = std::min(phases - 1, static_cast<size_t>(phase));
            const double begin = 2 * pi * low_phase / phases, end = 2 * pi * (low_phase + 1) / phases;
            const double mix = std::clamp((eccentric - orbit.eccentricity * std::sin(eccentric) - begin
                + orbit.eccentricity * std::sin(begin)) / (end - begin - orbit.eccentricity * (std::sin(end) - std::sin(begin))), 0.0, 1.0);
            const double row = std::clamp((latitude + 1) * .5 * (rows - 1), 0.0, double(rows - 1));
            const size_t low = static_cast<size_t>(row), high = std::min(low + 1, rows - 1);
            longitude = std::fmod(longitude / (2 * pi) + 1, 1.0) * columns;
            if (longitude < 0) longitude += columns;
            const size_t left = static_cast<size_t>(longitude), right = (left + 1) % columns;
            const auto value = [&](size_t r, size_t c)
            {
                const auto& curves = histories[r * columns + c];
                auto upper = std::lower_bound(curves.begin(), curves.end(), baseline,
                    [](const Curve& curve, double temperature) { return curve.baseline < temperature; });
                if (upper == curves.end()) upper = curves.end() - 1;
                const auto lower = upper == curves.begin() ? upper : upper - 1;
                const auto at = [&](const Curve& curve)
                {
                    return curve.energy[low_phase] + mix * (curve.energy[low_phase + 1] - curve.energy[low_phase]);
                };
                const double fraction = upper == lower ? 0 : std::clamp((baseline - lower->baseline) / (upper->baseline - lower->baseline), 0.0, 1.0);
                return at(*lower) + fraction * (at(*upper) - at(*lower));
            };
            const double a = value(low, left) + (longitude - left) * (value(low, right) - value(low, left));
            const double b = value(high, left) + (longitude - left) * (value(high, right) - value(high, left));
            return a + (row - low) * (b - a);
        }

        IceForcing forcing_at(double latitude, double longitude, double mean_anomaly, double regional_offset = 0) const
        {
            const double baseline = baseline_at(latitude, longitude) + regional_offset;
            const double row = std::clamp((latitude + 1) * .5 * (rows - 1), 0.0, double(rows - 1));
            const size_t low = static_cast<size_t>(row), high = std::min(low + 1, rows - 1);
            longitude = std::fmod(longitude / (2 * pi) + 1, 1.0) * columns;
            if (longitude < 0) longitude += columns;
            const size_t left = static_cast<size_t>(longitude), right = (left + 1) % columns;
            const double eccentric = orbit.eccentric_anomaly(mean_anomaly);
            const auto blend = [](const IceForcing& a, const IceForcing& b, double fraction)
            {
                return IceForcing{a.shortwave + fraction * (b.shortwave - a.shortwave),
                    a.outgoing_at_freezing + fraction * (b.outgoing_at_freezing - a.outgoing_at_freezing),
                    a.feedback + fraction * (b.feedback - a.feedback),
                    a.transported + fraction * (b.transported - a.transported),
                    a.ocean_heat + fraction * (b.ocean_heat - a.ocean_heat)};
            };
            return blend(blend(forcing(low, left, eccentric, baseline), forcing(low, right, eccentric, baseline), longitude - left),
                blend(forcing(high, left, eccentric, baseline), forcing(high, right, eccentric, baseline), longitude - left), row - low);
        }
    };

    // Bound seasonal preparation independently of the number of water worlds in
    // a catalog. Render calls never wait for a model, and never create a thread
    // per planet. Jobs own only copied physical inputs, so deletion is safe.
    class IceSeasonQueue
    {
        using Task = std::packaged_task<std::shared_ptr<const IceSeason>()>;
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<Task> jobs;
        bool stopping = false;
        std::thread worker;
        void run()
        {
            for (;;)
            {
                Task task;
                {
                    std::unique_lock<std::mutex> guard(mutex);
                    wake.wait(guard, [&]() { return stopping || !jobs.empty(); });
                    if (stopping && jobs.empty()) return;
                    task = std::move(jobs.front());
                    jobs.pop_front();
                }
                task();
            }
        }
    public:
        IceSeasonQueue() : worker([this]() { run(); }) { }
        ~IceSeasonQueue()
        {
            {
                std::lock_guard<std::mutex> guard(mutex);
                stopping = true;
            }
            wake.notify_one();
            worker.join();
        }
        std::shared_future<std::shared_ptr<const IceSeason>> submit(std::function<std::shared_ptr<const IceSeason>()> function)
        {
            std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
            if (!guard || jobs.size() >= 8) return {};
            Task task(std::move(function));
            auto result = task.get_future().share();
            jobs.push_back(std::move(task));
            wake.notify_one();
            return result;
        }
        static IceSeasonQueue& instance() { static IceSeasonQueue value; return value; }
    };
}
