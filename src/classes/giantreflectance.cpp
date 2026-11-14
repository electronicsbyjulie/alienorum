#include "giantreflectance.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <random>
#include <vector>

namespace alienorum
{
    namespace
    {
        using json = nlohmann::json;
        constexpr size_t sample_count = 131;
        using Spectrum = std::array<double, sample_count>;
        struct Grid
        {
            bool valid = false;
            Spectrum wavelength{}, solar{};
            std::array<Spectrum, 3> filters{}, cmf{};
            std::array<std::array<std::array<Spectrum, 4>, 2>, 2> spectra{};
            Grid()
            {
                try
                {
                    std::ifstream stream("catalogs/giant_reflectance.json");
                    json data;
                    stream >> data;
                    if (data.at("format") != 1 || data.at("models").size() != 16) return;
                    wavelength = data.at("wavelengths_nm").get<Spectrum>();
                    solar = data.at("solar_flux").get<Spectrum>();
                    cmf = data.at("cmf_xyz").get<std::array<Spectrum, 3>>();
                    for (int band = 0; band < 3; ++band)
                        filters[band] = data.at("filters").at(band == 0 ? "B" : band == 1 ? "V" : "R").get<Spectrum>();
                    std::array<bool, 16> loaded{};
                    for (const auto& model : data.at("models"))
                    {
                        const std::string family_name = model.at("family");
                        if (family_name != "Jupiter" && family_name != "Neptune") return;
                        const int family = family_name == "Neptune";
                        const double metal = model.at("enrichment");
                        const int enrichment = metal == (family ? 30 : 3) ? 1 : 0;
                        if (metal != (family ? 10 : 1) && metal != (family ? 30 : 3)) return;
                        const double axis = model.at("axis_au");
                        int distance = axis == .8 ? 0 : axis == 2 ? 1 : axis == 5 ? 2 : axis == 10 ? 3 : -1;
                        if (distance < 0) return;
                        auto& present = loaded[family * 8 + enrichment * 4 + distance];
                        if (present) return;
                        present = true;
                        auto& values = spectra[family][enrichment][distance];
                        values = model.at("reflectance").get<Spectrum>();
                        for (double value : values)
                            if (!std::isfinite(value) || value < 0 || value > 2) return;
                    }
                    for (size_t i = 0; i < sample_count; ++i)
                    {
                        if (wavelength[i] != 350 + 5 * i || !std::isfinite(solar[i]) || solar[i] <= 0) return;
                        for (const auto& filter : filters)
                            if (!std::isfinite(filter[i]) || filter[i] < 0 || filter[i] > 1) return;
                        for (const auto& channel : cmf)
                            if (!std::isfinite(channel[i]) || channel[i] < 0) return;
                    }
                    valid = true;
                }
                catch (...) { valid = false; }
            }
        };

        const Grid& grid()
        {
            static const Grid result;
            return result;
        }

        double mix(double first, double second, double weight)
        {
            return first + (second - first) * weight;
        }

        Spectrum spectrum_at(const Grid& data, double family_weight, double metal, double axis)
        {
            constexpr std::array<double, 4> axes = {.8, 2, 5, 10};
            axis = std::clamp(axis, .8, 10.0);
            int lower = 0;
            while (lower < 2 && axis > axes[lower + 1]) ++lower;
            const double distance_weight = std::log(axis / axes[lower]) / std::log(axes[lower + 1] / axes[lower]);
            Spectrum result{};
            for (int family = 0; family < 2; ++family)
            {
                const double low = family ? 10 : 1;
                const double high = family ? 30 : 3;
                const double metal_weight = std::clamp(std::log(metal / low) / std::log(high / low), 0.0, 1.0);
                const double weight = family ? family_weight : 1 - family_weight;
                for (size_t i = 0; i < sample_count; ++i)
                {
                    const double first = mix(data.spectra[family][0][lower][i], data.spectra[family][0][lower + 1][i], distance_weight);
                    const double second = mix(data.spectra[family][1][lower][i], data.spectra[family][1][lower + 1][i], distance_weight);
                    result[i] += weight * mix(first, second, metal_weight);
                }
            }
            return result;
        }

        double planck_shape(double nm, double temperature)
        {
            const double x = 1.438776877e7 / (nm * temperature);
            return 1 / (std::pow(nm / 550, 5) * std::expm1(x));
        }

        std::array<double, 3> display_rgb(const Grid& data, const Spectrum& spectrum, double stellar_temperature)
        {
            std::array<double, 3> xyz{}, rgb{};
            for (size_t i = 0; i < sample_count; ++i)
            {
                const double illumination = planck_shape(data.wavelength[i], stellar_temperature)
                    / planck_shape(data.wavelength[i], 5778);
                const double quadrature = i == 0 || i + 1 == sample_count ? .5 : 1;
                const double flux = data.solar[i] * illumination * spectrum[i] * quadrature;
                for (int channel = 0; channel < 3; ++channel) xyz[channel] += flux * data.cmf[channel][i];
            }
            constexpr std::array<std::array<double, 3>, 3> matrix =
            {{{3.2406, -1.5372, -.4986}, {-.9689, 1.8758, .0415}, {.0557, -.2040, 1.0570}}};
            for (int channel = 0; channel < 3; ++channel)
                for (int component = 0; component < 3; ++component) rgb[channel] += matrix[channel][component] * xyz[component];
            const double peak = std::max({rgb[0], rgb[1], rgb[2], 1e-30});
            for (double& channel : rgb) channel = std::clamp(channel / peak, 0.0, 1.0);
            return rgb;
        }

        std::array<double, 2> photometry(const Grid& data, const Spectrum& spectrum, double stellar_temperature)
        {
            std::array<double, 3> reflected{}, reference{};
            for (size_t i = 0; i < sample_count; ++i)
            {
                const double wavelength = data.wavelength[i];
                // Preserve the supplied solar spectrum at 5778 K; other illuminants
                // use a blackbody shape ratio, an approximation without stellar spectra.
                const double illumination = planck_shape(wavelength, stellar_temperature) / planck_shape(wavelength, 5778);
                const double quadrature = i == 0 || i + 1 == sample_count ? .5 : 1;
                for (int band = 0; band < 3; ++band)
                {
                    const double flux = quadrature * data.solar[i] * data.filters[band][i];
                    reference[band] += flux;
                    reflected[band] += flux * illumination * spectrum[i];
                }
            }
            // Solar broadband zero-point calibration. Constant factors and the
            // 5 nm integration step cancel in these energy-response color ratios.
            return {.65 - 2.5 * std::log10((reflected[0] / reflected[1]) / (reference[0] / reference[1])),
                    .36 - 2.5 * std::log10((reflected[1] / reflected[2]) / (reference[1] / reference[2]))};
        }
    }

    json estimate_giant_reflectance(const GiantReflectanceInput& input)
    {
        const auto& data = grid();
        if (!data.valid || !std::isfinite(input.equivalent_axis_au) || input.equivalent_axis_au <= 0) return nullptr;
        std::mt19937 random(input.seed ^ 0x73706563u);
        const auto draw = [&]() { return random() / 4294967296.0; };
        const double mass = std::isfinite(input.earth_masses) && input.earth_masses > 0
            ? input.earth_masses : input.neptune ? 17 : 318;
        const double family = std::clamp(std::log(318 / mass) / std::log(318.0 / 17), 0.0, 1.0);
        const double stellar_metal = std::isfinite(input.stellar_metallicity_dex) ? input.stellar_metallicity_dex : 0;
        // Atmospheric enrichment is a scenario, not a measurement of the bulk interior.
        const double enrichment = std::clamp(3 * std::pow(318 / mass, .6)
            * std::pow(10.0, .25 * stellar_metal + .3 * (draw() - .5)), 1.0, 30.0);
        const double gravity = std::isfinite(input.gravity_m_s2) && input.gravity_m_s2 > 0 ? input.gravity_m_s2 : 15;
        const double cloud_shift = std::pow(gravity / 15, .04) * std::pow(10.0, .12 * (draw() - .5));
        const double carbon = std::isfinite(input.carbon_relative_to_iron_dex) ? input.carbon_relative_to_iron_dex : 0;
        const double methane_scale = std::isfinite(input.methane_fraction) && input.methane_fraction >= 0
            ? std::clamp(input.methane_fraction / (.0005 * enrichment), 0.0, 4.0)
            : std::pow(10.0, std::clamp(carbon * .3, -.5, .5));
        const double haze = std::isfinite(input.haze) && input.haze >= 0
            ? std::clamp(input.haze, 0.0, 1.0) : .08 + .3 * draw();
        const double temperature = std::isfinite(input.stellar_temperature_k) && input.stellar_temperature_k > 0
            ? std::clamp(input.stellar_temperature_k, 2000.0, 40000.0) : 5778;
        Spectrum mean{};
        json colors = json::array(), band_rgb = json::array();
        bool distance_clamped = false;
        // Cloud decks and photochemical blue absorbers vary between bands. These
        // bounded perturbations extend the library; they are not radiative-transfer fits.
        for (int band = 0; band < 8; ++band)
        {
            const double cloud = .04 + .12 * draw();
            const double pigment = haze * (.3 + 1.4 * draw());
            const double axis = input.equivalent_axis_au * cloud_shift * std::pow(10.0, .16 * (draw() - .5));
            distance_clamped = distance_clamped || axis < .8 || axis > 10;
            auto spectrum = spectrum_at(data, family, enrichment, axis);
            const Spectrum original = spectrum;
            for (size_t i = 0; i < sample_count; ++i)
            {
                const double wavelength = data.wavelength[i];
                if (wavelength >= 600)
                {
                    double continuum = original[i];
                    const size_t low = i > 12 ? i - 12 : 0, high = std::min(sample_count - 1, i + 12);
                    for (size_t j = low; j <= high; ++j) continuum = std::max(continuum, original[j]);
                    if (continuum > 0)
                    {
                        const double transmission = std::max(1e-6, original[i] / continuum);
                        spectrum[i] = continuum * std::pow(transmission, methane_scale);
                    }
                }
                const double absorber = std::exp(-pigment * std::pow(450 / wavelength, 4));
                spectrum[i] = mix(spectrum[i], .65, cloud) * absorber;
                mean[i] += spectrum[i] / 8;
            }
            colors.push_back(photometry(data, spectrum, temperature));
            band_rgb.push_back(display_rgb(data, spectrum, temperature));
        }
        const auto indices = photometry(data, mean, temperature);
        if (!std::isfinite(indices[0]) || !std::isfinite(indices[1])) return nullptr;
        return {{"model", "Cahoy2010_reflected_v1"}, {"status", "estimated"},
            {"source", "Cahoy_Marley_Fortney_2010_ApJ_724_189"}, {"phase_degrees", 0},
            {"BV", indices[0]}, {"VR", indices[1]}, {"bands", colors},
            {"rgb", display_rgb(data, mean, temperature)}, {"band_rgb", band_rgb},
            {"equivalent_axis_au", input.equivalent_axis_au}, {"enrichment_solar", enrichment},
            {"neptune_weight", family}, {"stellar_temperature_k", temperature},
            {"stellar_metallicity_used", std::isfinite(input.stellar_metallicity_dex)},
            {"stellar_carbon_used", std::isfinite(input.carbon_relative_to_iron_dex)},
            {"methane_supplied", std::isfinite(input.methane_fraction) && input.methane_fraction >= 0},
            {"methane_absorption_scale", methane_scale}, {"haze_scenario", haze},
            {"cloud_shift", cloud_shift}, {"seed", input.seed}, {"earth_masses_scenario", mass}, {"gravity_m_s2", gravity},
            {"enrichment_grid_clamped", (family < 1 && enrichment > 3) || (family > 0 && enrichment < 10)},
            {"distance_grid_clamped", distance_clamped},
            {"interpretation", "Reflection_grid_with_seeded_cloud_haze_and_methane_scenarios;Bessell_photometry_and_CIE1931_RGB"}};
    }

    bool valid_giant_reflectance(const json& model, double bv)
    {
        try
        {
            if (!model.is_object()) return false;
            const auto& color = model.contains("texture") ? model.at("texture") : model;
            if (model.at("model") != "Cahoy2010_reflected_v1"
                || !model.at("BV").is_number() || !model.at("VR").is_number()
                || std::abs(color.at("BV").get<double>() - bv) > 1e-9
                || !std::isfinite(bv) || !std::isfinite(model.at("VR").get<double>())
                || model.at("bands").size() != 8) return false;
            if (model.at("band_rgb").size() != 8 || model.at("rgb").size() != 3) return false;
            const auto rgb_valid = [](const json& color)
            {
                if (!color.is_array() || color.size() != 3) return false;
                for (const auto& channel : color)
                {
                    if (!channel.is_number()) return false;
                    const double value = channel.get<double>();
                    if (!std::isfinite(value) || value < 0 || value > 1) return false;
                }
                return true;
            };
            if (!rgb_valid(model.at("rgb"))) return false;
            if (model.contains("texture"))
            {
                const auto& texture = model.at("texture");
                if (!texture.at("VR").is_number() || !std::isfinite(texture.at("VR").get<double>())
                    || !rgb_valid(texture.at("rgb"))) return false;
            }
            for (const auto& color : model.at("band_rgb"))
                if (!rgb_valid(color)) return false;
            for (const auto& band : model.at("bands"))
            {
                if (!band.is_array() || band.size() != 2) return false;
                for (const auto& value : band)
                    if (!value.is_number() || !std::isfinite(value.get<double>())) return false;
            }
            return true;
        }
        catch (...) { return false; }
    }
}
