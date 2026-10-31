#ifndef ALIENORUM_FRAME_SCRATCH_H
#define ALIENORUM_FRAME_SCRATCH_H

#include "../performance_options.h"
#include <cstdint>
#include <vector>

namespace alienorum
{
    inline bool frame_scratch_reference = false;
    inline bool frame_scratch_enabled() { return performance_enabled(77) && !frame_scratch_reference; }

    // Storage only: every borrow starts empty. No geometry or material result survives reuse.
    template<class T> class FrameScratch
    {
        std::vector<T> values;
        uint64_t owner = 0;
        size_t width = 0, height = 0;
        const size_t budget;
        public:
        explicit FrameScratch(size_t bytes = 8u * 1024u * 1024u) : budget(bytes) {}
        std::vector<T>& acquire(uint64_t next_owner, size_t next_width, size_t next_height)
        {
            if (owner != next_owner || width != next_width || height != next_height
                || values.capacity() > budget / sizeof(T)) std::vector<T>().swap(values);
            owner = next_owner;
            width = next_width;
            height = next_height;
            values.clear();
            return values;
        }
        size_t retained_capacity() const { return values.capacity(); }
    };
}
#endif
