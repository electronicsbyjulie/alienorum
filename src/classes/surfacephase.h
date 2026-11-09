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
#include "misc.h"
#include "climate.h"
#include "surfaceclimate.h"

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

    inline std::array<unsigned char, 3> exposed_basin_color(double x, double y, double z)
    {
        // Continuous spherical geology, identical during generation and later drying.
        const double province = fBm(x * 4 + 71.2, y * 4 + 34.9, z * 4 + 6.1, 4, 2, .5);
        const double grain = fBm(x * 90 + 91.7, y * 90 + 43.1, z * 90 + 17.9, 4, 2, .5);
        const double shade = std::clamp(.35 + .55 * province + .35 * grain, .2, 1.0);
        return {static_cast<unsigned char>(150 * shade), static_cast<unsigned char>(115 * shade),
            static_cast<unsigned char>(78 * shade)};
    }

    struct SurfacePhasePixel
    {
        static constexpr size_t packed_bytes = 27;
        std::array<unsigned char, 3> base{}, frost{}, original{};
        unsigned char material = 0, original_material = 0;
        bool ocean = false, eligible = false, frozen_surface = false, active = true, evaporated = false;
        float ice = 0, temperature = NAN;
        double energy = NAN;
        float x = 0, y = 0, z = 0, elevation = 0;
        unsigned char surface_material() const
        {
            return frozen_surface || ice >= .5 ? (ocean ? 4 : 5) : ocean ? (evaporated ? 1 : 2) : original_material;
        }
        std::array<unsigned char, 3> color() const
        {
            std::array<unsigned char, 3> result;
            const auto basin = ocean && evaporated ? exposed_basin_color(x, y, z) : base;
            for (int channel = 0; channel < 3; ++channel)
            {
                const bool dormant = original_material == 3 && !active;
                const double ground = channel == 0 ? 105 : channel == 1 ? 85 : 60;
                const double unfrozen = ocean && evaporated ? basin[channel]
                    : dormant ? .35 * base[channel] + .65 * ground : base[channel];
                result[channel] = static_cast<unsigned char>(std::clamp(std::lround(
                    unfrozen * (1 - ice) + frost[channel] * ice), 0l, 255l));
            }
            return result;
        }
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
        tile.packed.resize(tile.pixels.size() * SurfacePhasePixel::packed_bytes);
        for (size_t i = 0; i < tile.pixels.size(); ++i)
        {
            const auto& pixel = tile.pixels[i];
            auto* bytes = tile.packed.data() + i * SurfacePhasePixel::packed_bytes;
            for (int channel = 0; channel < 3; ++channel)
            {
                bytes[channel] = pixel.base[channel]; bytes[channel + 3] = pixel.frost[channel]; bytes[channel + 6] = pixel.original[channel];
            }
            bytes[9] = pixel.original_material;
            bytes[10] = (pixel.eligible ? 1 : 0) | (pixel.ocean ? 2 : 0) | (pixel.frozen_surface ? 4 : 0)
                | (pixel.active ? 8 : 0) | (pixel.evaporated ? 16 : 0);
            std::memcpy(bytes + 11, &pixel.ice, sizeof(float));
            std::memcpy(bytes + 15, &pixel.energy, sizeof(double));
            std::memcpy(bytes + 23, &pixel.temperature, sizeof(float));
        }
        std::vector<SurfacePhasePixel>().swap(tile.pixels);
    }

    inline std::vector<SurfacePhasePixel> unpack_phase_tile(const SurfacePhaseTile& tile)
    {
        std::vector<SurfacePhasePixel> pixels(tile.packed.size() / SurfacePhasePixel::packed_bytes);
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            auto& pixel = pixels[i];
            const auto* bytes = tile.packed.data() + i * SurfacePhasePixel::packed_bytes;
            for (int channel = 0; channel < 3; ++channel)
            {
                pixel.base[channel] = bytes[channel]; pixel.frost[channel] = bytes[channel + 3]; pixel.original[channel] = bytes[channel + 6];
            }
            pixel.original_material = bytes[9]; pixel.eligible = bytes[10] & 1; pixel.ocean = bytes[10] & 2;
            pixel.frozen_surface = bytes[10] & 4; pixel.active = bytes[10] & 8;
            pixel.evaporated = bytes[10] & 16;
            std::memcpy(&pixel.ice, bytes + 11, sizeof(float));
            std::memcpy(&pixel.energy, bytes + 15, sizeof(double));
            std::memcpy(&pixel.temperature, bytes + 23, sizeof(float));
            pixel.material = pixel.surface_material();
        }
        return pixels;
    }

    struct SurfacePhaseSurvey
    {
        LocalClimate climate;
        unsigned long width = 0, height = 0;
        double lat_scale = 0, lon_scale = 0, freezing = 271.23, epoch = 0;
        uint64_t timeline = 0;
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
                if (!job->survey && job->timeline_token && job->timeline_token->load(std::memory_order_acquire) != job->timeline)
                {
                    job->ready.store(true, std::memory_order_release);
                    continue;
                }
                if (job->survey)
                {
                    auto& survey = *job->survey;
                    // Every temperature can change water or vegetation state. Hot regions
                    // must participate too, including basins boiling dry for the first time.
                    for (unsigned long row = 0; row < survey.height; row += 8)
                    {
                        for (unsigned long column = 0; column < survey.width; column += 16)
                        {
                            survey.candidates.push_back((uint64_t(row / 8) << 32) | (column / 16));
                        }
                    }
                    survey.ready.store(true, std::memory_order_release);
                    job->ready.store(true, std::memory_order_release);
                    continue;
                }
                const double elapsed = std::isfinite(job->tile.epoch) ? job->epoch - job->tile.epoch : -1;
                for (auto& pixel : job->tile.pixels)
                {
                    if (!pixel.eligible) continue;
                    const auto state = local_surface_state(job->climate, pixel.x, pixel.y, pixel.z, pixel.elevation,
                        pixel.ocean, job->salinity, job->pressure, pixel.ice, pixel.energy, elapsed);
                    pixel.temperature = state.temperature;
                    pixel.ice = state.ice;
                    pixel.energy = state.energy;
                    pixel.frozen_surface = state.frozen_surface;
                    pixel.active = state.vegetation_active;
                    pixel.evaporated = state.evaporated_surface;
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
        std::deque<uint64_t> retries;
        std::unordered_set<uint64_t> retry_keys;
        std::shared_ptr<SurfacePhaseSurvey> survey;
        std::vector<uint64_t> candidates;
        size_t candidate_cursor = 0;
        size_t catchup_candidate_start = 0;
        uint64_t revisit_revision = 0;
        unsigned int batch_limit = 3840;
        uint64_t timeline = 0, clock_revision = surface_phase_clock.load();
        bool continuous_clock = false;
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
                || (!continuous_clock && (epoch < requested_epoch || epoch - requested_epoch > 3600))))
            {
                ++timeline;
                timeline_token->store(timeline, std::memory_order_release);
                catching_up = true; catchup_surveyed = false;
                catchup_budget_ms = surface_phase_seek_budget_ms;
                catchup_revisited = revisions.empty(); catchup_revision = revision;
                catchup_remaining = revisions.size();
                // Screening contains coordinates, not dated pixel results. Keep
                // its work and traversal alive during repeated date controls.
                catchup_candidate_start = candidate_cursor;
                revisit_revision = 0;
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
            return index < tile->second.packed.size() / SurfacePhasePixel::packed_bytes
                ? tile->second.packed[index * SurfacePhasePixel::packed_bytes + 9] : fallback;
        }
        bool underlying_ocean(unsigned long column, unsigned long row) const
        {
            const auto tile = tiles.find(key(column / tile_width, row / tile_height));
            if (tile == tiles.end()) return false;
            const size_t index = (row - tile->second.rect.y) * tile->second.rect.width + column - tile->second.rect.x;
            if (index < tile->second.pixels.size()) return tile->second.pixels[index].ocean;
            return index < tile->second.packed.size() / SurfacePhasePixel::packed_bytes
                && (tile->second.packed[index * SurfacePhasePixel::packed_bytes + 10] & 2);
        }
    };
}
#endif
