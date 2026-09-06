#pragma once

#include <cstdint>

// Count at the actual Vulkan draw sites so instanced groups count once and
// skipped objects count zero. Reset before recording each frame. Thread-local
// storage reflects the renderer's single command-recording thread.
struct DrawStatistics {
    static inline thread_local uint32_t calls = 0;
};
