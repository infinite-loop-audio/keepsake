#include "vst2_string_buffer.h"

#include <cassert>
#include <cstdint>
#include <cstring>

int main() {
    struct GuardedDestination {
        char value[16] = {};
        std::uint64_t guard = 0xA5A5A5A5A5A5A5A5ULL;
    } destination;

    query_vst2_string(destination.value, [](char *buffer) {
        std::memset(buffer, 'x', 64);
        buffer[64] = '\0';
    });

    assert(std::strlen(destination.value) == sizeof(destination.value) - 1);
    assert(destination.guard == 0xA5A5A5A5A5A5A5A5ULL);
    return 0;
}
