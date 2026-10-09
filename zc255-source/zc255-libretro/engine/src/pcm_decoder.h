#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace enhanced_music {
struct PcmAsset {
    std::string path;
    std::array<uint8_t,32> hash{};
    std::vector<float> stereo;
    std::vector<uint8_t> encoded; // Immutable GME input, never decoder memory.
    unsigned rate=0;
    int type=0,track=0,track_count=1;
    uint64_t frames() const { return stereo.size()/2; }
};
// Reads/hash-verifies encoded source even on warm cache hit. No cache-history
// requirement: missing cache fully decodes the same file from its beginning.
std::shared_ptr<const PcmAsset> decode(const std::string& path,int track,
    const std::array<uint8_t,32>* expected,std::string& error);
void clear_decode_cache();
}
