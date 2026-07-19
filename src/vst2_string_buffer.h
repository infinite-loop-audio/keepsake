#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

// Some VST2 plugins write modern-length parameter labels even though the
// legacy API documents much smaller host buffers. Keep plugin writes away
// from IPC structs and truncate only after the dispatcher returns.
template <std::size_t DestinationSize, typename Query>
void query_vst2_string(char (&destination)[DestinationSize], Query &&query) {
    static_assert(DestinationSize > 0);
    std::array<char, 4096> scratch{};
    std::forward<Query>(query)(scratch.data());
    scratch.back() = '\0';

    const std::size_t copy_size =
        std::min(std::strlen(scratch.data()), DestinationSize - 1);
    std::memcpy(destination, scratch.data(), copy_size);
    destination[copy_size] = '\0';
}
