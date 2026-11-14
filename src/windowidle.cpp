#include "windowidle.h"

#if defined(__linux__)
#include <SDL_syswm.h>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#if defined(SDL_VIDEO_DRIVER_X11)
namespace
{
    struct EventWakePipe
    {
        int descriptors[2] = {-1, -1};
        bool active = false;
        std::atomic<bool> pending{false};

        static int SDLCALL wake(void* data, SDL_Event* event)
        {
            if (event->type != SDL_POLLSENTINEL)
            {
                auto* pipe = static_cast<EventWakePipe*>(data);
                pipe->pending = true;
                const char byte = 1;
                const auto written = write(pipe->descriptors[1], &byte, 1);
                (void)written;
            }
            return 1;
        }

        EventWakePipe()
        {
            if (pipe(descriptors) != 0) return;
            for (int descriptor : descriptors)
            {
                const int flags = fcntl(descriptor, F_GETFL);
                if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0) return;
                fcntl(descriptor, F_SETFD, FD_CLOEXEC);
            }
            SDL_AddEventWatch(wake, this);
            active = true;
        }

        ~EventWakePipe()
        {
            if (active) SDL_DelEventWatch(wake, this);
            for (int descriptor : descriptors) if (descriptor >= 0) close(descriptor);
        }
    };
}
#endif
#endif

bool wait_for_window_event(SDL_Window* window, SDL_Event* event)
{
#if defined(__linux__) && defined(SDL_VIDEO_DRIVER_X11)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_X11)
    {
        EventWakePipe wake;
        if (wake.active)
        {
            pollfd descriptors[2] =
            {
                {ConnectionNumber(info.info.x11.display), POLLIN, 0},
                {wake.descriptors[0], POLLIN, 0}
            };
            for (;;)
            {
                const bool pushed = wake.pending.exchange(false);
                char bytes[128];
                while (read(wake.descriptors[0], bytes, sizeof(bytes)) > 0) {}
                if (SDL_PollEvent(event)) return true;
                // Event watches run just before insertion into SDL's queue.
                // Let SDL finish that handoff if the producer woke us early.
                if (pushed || wake.pending.load()) return SDL_WaitEvent(event) != 0;
                const int result = poll(descriptors, 2, -1);
                if (result < 0 && errno == EINTR) continue;
                if (result < 0 || (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL))) break;
            }
        }
    }
#else
    (void)window;
#endif
    return SDL_WaitEvent(event) != 0;
}
