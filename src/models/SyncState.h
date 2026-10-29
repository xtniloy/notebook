#pragma once

#include <cstdint>

namespace notes::models {

// `sync_state` column (Plan §7). Stored as the integer value.
enum class SyncState : std::uint8_t {
    Dirty = 0,  // changed locally since the last upload
    Synced = 1, // matches the last uploaded version
};

} // namespace notes::models
