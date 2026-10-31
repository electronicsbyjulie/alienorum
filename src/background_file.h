#ifndef ALIENORUM_BACKGROUND_FILE_H
#define ALIENORUM_BACKGROUND_FILE_H

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <functional>
#include <optional>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif
#endif

namespace alienorum
{
    // One active owned job; producers wait before constructing another snapshot.
    class BackgroundFile
    {
        std::mutex mutex;
        std::future<bool> pending;
        std::mutex queue_mutex;
        std::function<bool()> queued;
        bool running = false;
        bool finish_locked()
        {
            if (!pending.valid()) return true;
            try { return pending.get(); }
            catch (...) { return false; }
        }
        public:
        template<class Capture> bool submit(Capture capture)
        {
            std::lock_guard<std::mutex> lock(mutex);
            finish_locked();
            try
            {
                auto task = capture();
                pending = std::async(std::launch::async, [task = std::make_optional(std::move(task))]() mutable
                {
                    bool ok = false;
                    try { ok = (*task)(); } catch (...) {}
                    task.reset();
                    if (!ok) std::cerr << "Background file publication failed." << std::endl;
                    return ok;
                });
                return true;
            }
            catch (...) { return false; }
        }
        // Coalesce repeated requests into one follow-up containing the latest state.
        template<class Task> bool submit_latest(Task task)
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::lock_guard<std::mutex> queue_lock(queue_mutex);
            if (running)
            {
                queued = std::move(task);
                return true;
            }
            finish_locked();
            running = true;
            try
            {
                pending = std::async(std::launch::async, [this, task = std::function<bool()>(std::move(task))]() mutable
                {
                    bool ok = true;
                    while (task)
                    {
                        try { if (!task()) ok = false; }
                        catch (...) { ok = false; }
                        std::lock_guard<std::mutex> queue_lock(queue_mutex);
                        task = std::move(queued);
                        queued = {};
                        if (!task) running = false;
                    }
                    return ok;
                });
                return true;
            }
            catch (...)
            {
                running = false;
                return false;
            }
        }
        template<class Assist> bool finish_with(Assist assist)
        {
            std::lock_guard<std::mutex> lock(mutex);
            while (pending.valid() && pending.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            {
                assist();
            }
            return finish_locked();
        }
        bool finish()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return finish_locked();
        }
        ~BackgroundFile() { finish(); }
    };

    template<class Write> bool publish_file(const std::filesystem::path& destination, Write write)
    {
        static std::atomic<unsigned long long> sequence{0};
        auto temporary = destination;
        temporary += ".alienorum-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(sequence.fetch_add(1)) + ".tmp";
        bool ok = false;
        try
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (stream)
            {
                ok = write(stream);
                stream.flush();
                ok = ok && stream.good();
                stream.close();
                ok = ok && !stream.fail();
            }
            if (ok)
            {
#ifdef _WIN32
                ok = MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
                std::error_code error;
                std::filesystem::rename(temporary, destination, error);
                ok = !error;
#endif
            }
        }
        catch (...) { ok = false; }
        std::error_code error;
        std::filesystem::remove(temporary, error);
        return ok;
    }
}
#endif
