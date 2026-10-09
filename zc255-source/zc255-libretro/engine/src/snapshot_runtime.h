#pragma once
#include "zc/savestate_serialize.h"

// Values owned by zc_sys.cpp, including its private edge history and item caches.
// Read/skip fully decode their section before changing any live value.
void zc255_runtime_write(SaveStateBlob& blob);
bool zc255_runtime_read(SaveStateBlob& blob);
bool zc255_runtime_skip(SaveStateBlob& blob);

void zc255_cheats_write(SaveStateBlob& blob);
bool zc255_cheats_read(SaveStateBlob& blob);
bool zc255_cheats_skip(SaveStateBlob& blob);

// Explicitly opt-in engine regression; restores original game state before return.
bool zc255_inventory_selftest();
bool zc255_resources_selftest();
bool zc255_external_io_blocked();
