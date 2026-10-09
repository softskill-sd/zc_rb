#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "materializer.h"
#include "embedded_catalog.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace zc255_assets {
namespace {
bool equal_file(const std::filesystem::path& path, const unsigned char* bytes, size_t size) {
    std::error_code ec;
    if(!std::filesystem::is_regular_file(path,ec) || ec || std::filesystem::file_size(path,ec)!=size || ec) return false;
    std::ifstream stream(path,std::ios::binary);
    if(!stream) return false;
    std::array<char,65536> buffer;
    for(size_t offset=0;offset<size;) {
        auto count=std::min(buffer.size(),size-offset);
        if(!stream.read(buffer.data(),static_cast<std::streamsize>(count)) || std::memcmp(buffer.data(),bytes+offset,count)) return false;
        offset+=count;
    }
    return true;
}
}
bool prepare(const std::filesystem::path& save_root, std::filesystem::path& asset_root, std::string& error) {
    try {
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&prepare),&module)) throw std::runtime_error("Cannot locate embedded core resources");
        // 128-bit version directory keeps legacy engine paths below MAX_PATH
        // for ordinary RetroArch save roots; contents are still compared exactly.
        asset_root=save_root/"zc255-assets"/std::string(identity,32);
        std::filesystem::create_directories(asset_root);
        for(const auto& entry:entries) {
            std::filesystem::path relative=std::filesystem::u8path(entry.path);
            if(relative.is_absolute() || relative.has_root_path()) throw std::runtime_error("Invalid embedded asset path");
            for(const auto& component:relative) if(component=="..") throw std::runtime_error("Invalid embedded asset traversal");
            HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(entry.resource_id),MAKEINTRESOURCEW(10));
            if(!resource || SizeofResource(module,resource)!=entry.size) throw std::runtime_error("Embedded asset missing or truncated: "+std::string(entry.path));
            auto loaded=LoadResource(module,resource);
            auto bytes=static_cast<const unsigned char*>(LockResource(loaded));
            if(!bytes && entry.size) throw std::runtime_error("Cannot read embedded asset: "+std::string(entry.path));
            auto target=asset_root/relative;
            // Compare actual bytes. A stale or damaged cache is repaired, never trusted by name alone.
            if(equal_file(target,bytes,entry.size)) continue;
            std::filesystem::create_directories(target.parent_path());
            auto temporary=target.parent_path()/(L".zc255-tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetCurrentThreadId()));
            {
                std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
                if(!output) throw std::runtime_error("Cannot create embedded asset cache file: "+std::string(entry.path));
                output.write(reinterpret_cast<const char*>(bytes),static_cast<std::streamsize>(entry.size));
                output.close();
                if(!output) { std::error_code ignored;std::filesystem::remove(temporary,ignored);throw std::runtime_error("Cannot write embedded asset: "+std::string(entry.path)); }
            }
            if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
                auto code=GetLastError();std::error_code ignored;std::filesystem::remove(temporary,ignored);
                throw std::system_error(code,std::system_category(),"Cannot publish embedded asset cache");
            }
            if(!equal_file(target,bytes,entry.size)) throw std::runtime_error("Embedded asset cache verification failed: "+std::string(entry.path));
        }
        return true;
    } catch(const std::exception& failure) { error=failure.what();return false; }
}
}
