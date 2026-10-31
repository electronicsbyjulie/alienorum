#ifndef ALIENORUM_UNIVERSE_SAVE_PROGRESS_H
#define ALIENORUM_UNIVERSE_SAVE_PROGRESS_H

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace alienorum
{
    enum class UniverseSavePhase { preparing, saving };
    struct UniverseSaveActivity
    {
        std::atomic<UniverseSavePhase> phase{UniverseSavePhase::preparing};
    };

    inline std::mutex universe_save_activity_mutex;
    // The queued or running job owns the activity, so completion and coalescing
    // both remove the indicator without leaving a stale window behind.
    inline std::weak_ptr<UniverseSaveActivity> displayed_universe_save;

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

    inline std::optional<UniverseSavePhase> universe_save_phase()
    {
        std::lock_guard<std::mutex> lock(universe_save_activity_mutex);
        auto activity = displayed_universe_save.lock();
        if (!activity) return {};
        return activity->phase.load();
    }
}
#endif
