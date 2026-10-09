#include "snapshot_runtime.h"

namespace {
using CheatQueue = std::queue<std::tuple<Cheat, int, int, std::string>>;
bool decode_cheat_queue(SaveStateBlob& b, CheatQueue& result) {
    uint32_t count = 0; b.get(count);
    if (b.failed || count > 4096 || count > (b.data.size() - b.pos) / 16) {
        b.failed = true; return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        int32_t cheat = 0; int a = 0, c = 0; std::string text;
        b.get(cheat); b.get(a); b.get(c); b.get_str(text);
        if (b.failed || cheat < None || cheat >= Last) { b.failed = true; return false; }
        result.emplace(static_cast<Cheat>(cheat), a, c, std::move(text));
    }
    return true;
}
}
void zc255_cheats_write(SaveStateBlob& b) {
    auto pending = cheats;
    if (pending.size() > 4096) { b.failed = true; return; }
    b.put(uint32_t(pending.size()));
    while (!pending.empty()) {
        const auto& [cheat, a, c, text] = pending.front();
        b.put(int32_t(cheat)); b.put(a); b.put(c); b.put_str(text); pending.pop();
    }
}
bool zc255_cheats_read(SaveStateBlob& b) {
    CheatQueue pending;
    if (!decode_cheat_queue(b, pending)) return false;
    cheats.swap(pending); return true;
}
bool zc255_cheats_skip(SaveStateBlob& b) {
    CheatQueue pending; return decode_cheat_queue(b, pending);
}
