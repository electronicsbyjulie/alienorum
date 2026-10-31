#ifndef ALIENORUM_CATALOG_CAPTURE_H
#define ALIENORUM_CATALOG_CAPTURE_H

#include "universe_save_progress.h"
#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <memory>
#include <thread>

extern std::recursive_mutex catalog_mutex;

namespace alienorum
{
    // Capture steps release the catalog between objects. An importer can service requests
    // itself while holding the recursive catalog lock, avoiding a join deadlock.
    class CatalogCapture
    {
        std::mutex mutex;
        std::function<void()> request;
        public:
        bool assist()
        {
            std::unique_lock<std::recursive_mutex> catalog(catalog_mutex, std::try_to_lock);
            if (!catalog.owns_lock()) return false;
            std::lock_guard<std::mutex> lock(mutex);
            if (!request) return false;
            auto task = std::move(request);
            request = {};
            task();
            return true;
        }
        void access(const std::function<void()>& action)
        {
            auto task = std::make_shared<std::packaged_task<void()>>(action);
            auto result = task->get_future();
            {
                std::lock_guard<std::mutex> lock(mutex);
                request = [task]() { (*task)(); };
            }
            while (result.wait_for(std::chrono::microseconds(0)) != std::future_status::ready)
            {
                if (!assist())
                {
                    if (current_universe_save) current_universe_save->waiting_for_catalog = true;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                else
                {
                    if (current_universe_save) current_universe_save->waiting_for_catalog = false;
                    std::this_thread::yield();
                }
            }
            if (current_universe_save) current_universe_save->waiting_for_catalog = false;
            result.get();
        }
    };

    inline thread_local CatalogCapture* catalog_capture = nullptr;
    template<class Action> void capture_access(Action action)
    {
        if (catalog_capture) catalog_capture->access(action);
        else action();
    }
}
#endif
