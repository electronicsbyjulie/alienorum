#ifndef ALIENORUM_SURFACEPHASE_H
#define ALIENORUM_SURFACEPHASE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "climate.h"
#include "salinity.h"

namespace alienorum
{
    inline bool dynamic_surface_ice = true;
    inline std::atomic<uint64_t> surface_phase_clock{0};
    inline unsigned int surface_phase_seek_budget_ms = 20;
    struct SurfacePhaseRect
    {
        unsigned long x = 0, y = 0, width = 0, height = 0;
        uint64_t revision = 0;
    };

    struct SurfacePhasePixel
    {
        std::array<unsigned char, 3> base{}, frost{}, original{};
        unsigned char material = 0, original_material = 0;
        bool ocean = false, eligible = false;
        float ice = 0;
        float x = 0, y = 0, z = 0, elevation = 0;
    };

    struct SurfacePhaseTile
    {
        SurfacePhaseRect rect;
        std::vector<SurfacePhasePixel> pixels;
        std::vector<unsigned char> packed;
        std::list<uint64_t>::iterator lru;
        double epoch = NAN;
        uint64_t last_use = 0;
        uint64_t timeline = 0;
    };

    inline void pack_phase_tile(SurfacePhaseTile& tile)
    {
        tile.packed.resize(tile.pixels.size() * 15);
        for (size_t i = 0; i < tile.pixels.size(); ++i)
        {
            const auto& pixel = tile.pixels[i];
            auto* bytes = tile.packed.data() + i * 15;
            for (int channel = 0; channel < 3; ++channel)
            {
                bytes[channel] = pixel.base[channel]; bytes[channel + 3] = pixel.frost[channel]; bytes[channel + 6] = pixel.original[channel];
            }
            bytes[9] = pixel.original_material;
            bytes[10] = (pixel.eligible ? 1 : 0) | (pixel.ocean ? 2 : 0);
            std::memcpy(bytes + 11, &pixel.ice, sizeof(float));
        }
        std::vector<SurfacePhasePixel>().swap(tile.pixels);
    }

    inline std::vector<SurfacePhasePixel> unpack_phase_tile(const SurfacePhaseTile& tile)
    {
        std::vector<SurfacePhasePixel> pixels(tile.packed.size() / 15);
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            auto& pixel = pixels[i];
            const auto* bytes = tile.packed.data() + i * 15;
            for (int channel = 0; channel < 3; ++channel)
            {
                pixel.base[channel] = bytes[channel]; pixel.frost[channel] = bytes[channel + 3]; pixel.original[channel] = bytes[channel + 6];
            }
            pixel.original_material = bytes[9]; pixel.eligible = bytes[10] & 1; pixel.ocean = bytes[10] & 2;
            std::memcpy(&pixel.ice, bytes + 11, sizeof(float));
            pixel.material = pixel.ice >= .5 ? (pixel.ocean ? 4 : 5) : pixel.ocean ? 2 : pixel.original_material;
        }
        return pixels;
    }

    struct SurfacePhaseSurvey
    {
        LocalClimate climate;
        unsigned long width = 0, height = 0;
        double lat_scale = 0, lon_scale = 0, freezing = 271.23, epoch = 0;
        std::vector<uint64_t> candidates;
        std::atomic<bool> ready{false};
    };

    struct SurfacePhaseJob
    {
        SurfacePhaseTile tile;
        LocalClimate climate;
        unsigned int generation = 0;
        double epoch = 0, salinity = 35, pressure = 101325;
        std::atomic<bool> ready{false};
        std::shared_ptr<SurfacePhaseSurvey> survey;
        uint64_t timeline = 0;
        std::shared_ptr<std::atomic<uint64_t>> timeline_token;
    };

    inline double surface_ice_step(double ice, double temperature, double salinity, double pressure,
        double elapsed, bool ocean)
    {
        const double salt_ratio = salinity / std::max(1.0, 1000 - salinity);
        const double brine = ocean ? std::min(264.0, 1000 * salt_ratio / (std::max(.01, 1 - ice) + salt_ratio)) : 0;
        const double freezing = seawater_freezing_k(brine, pressure);
        if (!std::isfinite(elapsed) || elapsed < 0)
        {
            if (!ocean) return std::clamp((freezing + .25 - temperature) / .5, 0.0, 1.0);
            if (temperature >= seawater_freezing_k(salinity, pressure)) return 0;
            if (temperature <= seawater_freezing_k(salinity > 0 ? 264 : 0, pressure)) return 1;
            double low = 0, high = 1;
            for (int iteration = 0; iteration < 16; ++iteration)
            {
                const double middle = (low + high) * .5;
                const double concentration = std::min(264.0, 1000 * salt_ratio / (std::max(.01, 1 - middle) + salt_ratio));
                if (temperature < seawater_freezing_k(concentration, pressure)) low = middle;
                else high = middle;
            }
            return (low + high) * .5;
        }
        // 10 W/m2/K exchange, latent heat 334 kJ/kg, and a 10 cm ice slab.
        // A thin land frost layer responds faster than floating ocean ice.
        const double latent = ocean ? 917 * .10 * 334000 : 917 * .01 * 334000;
        const double offset = temperature - freezing;
        const double driving = std::abs(offset) <= .15 ? 0 : offset - std::copysign(.15, offset);
        return std::clamp(ice - 10 * driving * std::min(elapsed, 3.15576e10) / latent, 0.0, 1.0);
    }

    class SurfacePhaseWorker
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<std::shared_ptr<SurfacePhaseJob>> jobs;
        bool stopping = false;
        std::thread worker;

        void run()
        {
            for (;;)
            {
                std::shared_ptr<SurfacePhaseJob> job;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock, [&]() { return stopping || !jobs.empty(); });
                    if (stopping && jobs.empty()) return;
                    job = jobs.front();
                    jobs.pop_front();
                }
                if (job->timeline_token && job->timeline_token->load(std::memory_order_acquire) != job->timeline)
                {
                    job->ready.store(true, std::memory_order_release);
                    continue;
                }
                if (job->survey)
                {
                    auto& survey = *job->survey;
                    // Screen geographic tiles on the worker, without reading any map.
                    // The broad band includes seasonal forcing and elevation margins.
                    for (unsigned long y = 0; y < survey.height; y += 64)
                        for (unsigned long x = 0; x < survey.width; x += 128)
                        {
                            double low = INFINITY, high = -INFINITY;
                            for (const auto offset : {std::make_pair(0.0, 0.0), std::make_pair(128.0, 0.0),
                                std::make_pair(0.0, 64.0), std::make_pair(128.0, 64.0), std::make_pair(64.0, 32.0)})
                            {
                                const double lat = std::clamp(half_pi - (y + offset.second) / survey.lat_scale, -half_pi, half_pi);
                                const double lon = (x + offset.first) / survey.lon_scale - _pi;
                                const double temperature = survey.climate.temperature(-cos(lat) * cos(lon), -cos(lat) * sin(lon),
                                    sin(lat), survey.climate.sea_level_m);
                                low = std::min(low, temperature); high = std::max(high, temperature);
                            }
                            if (low <= survey.freezing + 60 && high >= survey.freezing - 60)
                                for (unsigned long row = y; row < std::min(y + 64, survey.height); row += 8)
                                    for (unsigned long column = x; column < std::min(x + 128, survey.width); column += 16)
                                        survey.candidates.push_back((uint64_t(row / 8) << 32) | (column / 16));
                        }
                    survey.ready.store(true, std::memory_order_release);
                    job->ready.store(true, std::memory_order_release);
                    continue;
                }
                const double elapsed = std::isfinite(job->tile.epoch) ? job->epoch - job->tile.epoch : -1;
                for (auto& pixel : job->tile.pixels)
                {
                    if (!pixel.eligible) continue;
                    const double temperature = job->climate.temperature(pixel.x, pixel.y, pixel.z, pixel.elevation);
                    pixel.ice = surface_ice_step(pixel.ice, temperature, job->salinity, job->pressure, elapsed, pixel.ocean);
                }
                job->tile.epoch = job->epoch;
                job->ready.store(true, std::memory_order_release);
            }
        }

    public:
        SurfacePhaseWorker() : worker([this]() { run(); }) { }
        ~SurfacePhaseWorker()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = true;
            }
            wake.notify_one();
            worker.join();
        }
        bool submit(const std::shared_ptr<SurfacePhaseJob>& job)
        {
            std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
            if (!lock || jobs.size() >= 7680) return false;
            if (job->survey) jobs.push_front(job);
            else jobs.push_back(job);
            lock.unlock();
            wake.notify_one();
            return true;
        }
        static SurfacePhaseWorker& instance()
        {
            static SurfacePhaseWorker value;
            return value;
        }
    };

    class SurfacePhase
    {
    public:
        static constexpr unsigned long tile_width = 16, tile_height = 8;
        static constexpr size_t tile_limit = 4096;
        unsigned int generation = 0;
        uint64_t revision = 0, visits = 0, scan = 0;
        unsigned int source_generation = 0, cloud_generation = 0;
        bool composite = false;
        std::unordered_map<uint64_t, SurfacePhaseTile> tiles;
        std::map<uint64_t, uint64_t> revisions;
        std::list<uint64_t> active;
        std::deque<SurfacePhaseRect> recent;
        mutable double focus_lat = NAN, focus_lon = NAN, focus_radius = 0, focus_distance = 0;
        mutable uint64_t focus_checked = 0, focus_revision = 0;
        std::shared_ptr<SurfacePhaseJob> pending;
        std::deque<std::shared_ptr<SurfacePhaseJob>> queued;
        std::unordered_set<uint64_t> inflight;
        std::shared_ptr<SurfacePhaseSurvey> survey;
        std::vector<uint64_t> candidates;
        size_t candidate_cursor = 0;
        uint64_t revisit_revision = 0;
        unsigned int batch_limit = 3840;
        uint64_t timeline = 0, clock_revision = surface_phase_clock.load();
        double requested_epoch = NAN;
        std::shared_ptr<std::atomic<uint64_t>> timeline_token = std::make_shared<std::atomic<uint64_t>>(0);
        bool catching_up = false, catchup_surveyed = false, catchup_revisited = false;
        unsigned int catchup_budget_ms = 20;
        uint64_t catchup_revision = 0;
        size_t catchup_remaining = 0;
        std::chrono::steady_clock::time_point surveyed{};
        LocalClimate climate;
        double climate_epoch = NAN, salinity = 35, pressure = 101325;
        std::chrono::steady_clock::time_point prepared{};
        uint64_t published_pixels = 0, published_patches = 0;
        void sync_epoch(double epoch)
        {
            const uint64_t current_clock = surface_phase_clock.load();
            if (std::isfinite(requested_epoch) && (current_clock != clock_revision
                || epoch < requested_epoch || epoch - requested_epoch > 3600))
            {
                ++timeline;
                timeline_token->store(timeline, std::memory_order_release);
                catching_up = true; catchup_surveyed = false;
                catchup_budget_ms = surface_phase_seek_budget_ms;
                catchup_revisited = revisions.empty(); catchup_revision = revision;
                catchup_remaining = revisions.size();
                survey.reset();
                candidates.clear();
                candidate_cursor = 0; revisit_revision = 0; scan = 0;
                surveyed = {};
            }
            requested_epoch = epoch;
            clock_revision = current_clock;
        }
        static uint64_t key(unsigned long x, unsigned long y) { return (uint64_t(y) << 32) | x; }
        unsigned char original_material(unsigned long column, unsigned long row, unsigned char fallback) const
        {
            const auto tile = tiles.find(key(column / tile_width, row / tile_height));
            if (tile == tiles.end()) return fallback;
            const size_t index = (row - tile->second.rect.y) * tile->second.rect.width + column - tile->second.rect.x;
            if (index < tile->second.pixels.size()) return tile->second.pixels[index].original_material;
            return index < tile->second.packed.size() / 15 ? tile->second.packed[index * 15 + 9] : fallback;
        }
        bool underlying_ocean(unsigned long column, unsigned long row) const
        {
            const auto tile = tiles.find(key(column / tile_width, row / tile_height));
            if (tile == tiles.end()) return false;
            const size_t index = (row - tile->second.rect.y) * tile->second.rect.width + column - tile->second.rect.x;
            if (index < tile->second.pixels.size()) return tile->second.pixels[index].ocean;
            return index < tile->second.packed.size() / 15 && (tile->second.packed[index * 15 + 10] & 2);
        }
    };
}
#endif
