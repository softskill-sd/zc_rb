#pragma once
#include <filesystem>
#include <string>
namespace zc255_assets {
bool prepare(const std::filesystem::path& save_root, std::filesystem::path& asset_root, std::string& error);
}
