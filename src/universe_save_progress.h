#ifndef ALIENORUM_UNIVERSE_SAVE_PROGRESS_H
#define ALIENORUM_UNIVERSE_SAVE_PROGRESS_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace alienorum
{
    enum class UniverseSavePhase { preparing, saving };
    struct UniverseSaveProgress
    {
        UniverseSavePhase phase = UniverseSavePhase::preparing;
        std::string step = "Waiting for save worker", file, unit;
        uint64_t completed = 0, total = 0;
        double elapsed_seconds = 0, stalled_seconds = 0;
        bool waiting_for_catalog = false;
    };
    struct UniverseSaveActivity
    {
        std::atomic<UniverseSavePhase> phase{UniverseSavePhase::preparing};
        std::atomic<bool> waiting_for_catalog{false};
        std::mutex mutex;
        UniverseSaveProgress progress;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now(), advanced = started;
    };

    inline std::mutex universe_save_activity_mutex;
    inline std::weak_ptr<UniverseSaveActivity> displayed_universe_save;
    inline thread_local std::shared_ptr<UniverseSaveActivity> current_universe_save;

    struct UniverseSaveProgressScope
    {
        std::shared_ptr<UniverseSaveActivity> previous;
        explicit UniverseSaveProgressScope(std::shared_ptr<UniverseSaveActivity> activity)
            : previous(std::move(current_universe_save))
        {
            current_universe_save = std::move(activity);
        }
        ~UniverseSaveProgressScope() { current_universe_save = std::move(previous); }
    };

    inline void save_progress_step(UniverseSavePhase phase, const std::string& step, uint64_t total,
        const std::string& unit, const std::string& file = {})
    {
        if (!current_universe_save) return;
        auto& activity = *current_universe_save;
        std::lock_guard<std::mutex> lock(activity.mutex);
        activity.phase = phase;
        activity.progress = {phase, step, file, unit, 0, total};
        activity.advanced = std::chrono::steady_clock::now();
    }

    inline void save_progress_advance(uint64_t count = 1)
    {
        if (!current_universe_save) return;
        auto& activity = *current_universe_save;
        std::lock_guard<std::mutex> lock(activity.mutex);
        activity.progress.completed = std::min(activity.progress.total, activity.progress.completed + count);
        activity.advanced = std::chrono::steady_clock::now();
    }

    inline std::shared_ptr<UniverseSaveActivity> begin_universe_save_activity(bool display)
    {
        if (!display) return {};
        auto activity = std::make_shared<UniverseSaveActivity>();
        std::lock_guard<std::mutex> lock(universe_save_activity_mutex);
        displayed_universe_save = activity;
        return activity;
    }

    inline void dismiss_universe_save_progress()
    {
        std::lock_guard<std::mutex> lock(universe_save_activity_mutex);
        displayed_universe_save.reset();
    }

    inline std::optional<UniverseSaveProgress> universe_save_progress()
    {
        std::shared_ptr<UniverseSaveActivity> activity;
        {
            std::lock_guard<std::mutex> lock(universe_save_activity_mutex);
            activity = displayed_universe_save.lock();
        }
        if (!activity) return {};
        std::lock_guard<std::mutex> lock(activity->mutex);
        auto progress = activity->progress;
        progress.phase = activity->phase.load();
        progress.waiting_for_catalog = activity->waiting_for_catalog.load();
        const auto now = std::chrono::steady_clock::now();
        progress.elapsed_seconds = std::chrono::duration<double>(now - activity->started).count();
        progress.stalled_seconds = std::chrono::duration<double>(now - activity->advanced).count();
        return progress;
    }

    inline std::optional<UniverseSavePhase> universe_save_phase()
    {
        auto progress = universe_save_progress();
        if (!progress) return {};
        return progress->phase;
    }
}
#endif
