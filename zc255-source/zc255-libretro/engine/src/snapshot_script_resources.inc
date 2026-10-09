#include <set>
#include <memory>

namespace {
struct DirectorySnapshot {
    int32_t index = 0, owner_type = 0, owner = 0;
    std::string path, list_path;
    bool allocated = false;
    std::vector<std::pair<bool, std::string>> entries;
};
bool resource_flag(SaveStateBlob& b, bool& value) {
    uint8_t raw = 0; b.get(raw);
    if (raw > 1) b.failed = true;
    value = raw != 0; return !b.failed;
}
bool decode_directory(SaveStateBlob& b, DirectorySnapshot& d) {
    b.get(d.index); b.get_str(d.path); b.get(d.owner_type); b.get(d.owner);
    if (!resource_flag(b, d.allocated) || d.index < 0 || d.index >= MAX_USER_DIRS) {
        b.failed = true; return false;
    }
    if (d.allocated) {
        b.get_str(d.list_path);
        int32_t count = 0; b.get(count);
        if (b.failed || count < 0 || count > FLIST_SIZE || d.list_path.size() >= 1024) {
            b.failed = true; return false;
        }
        for (int32_t i = 0; i < count; ++i) {
            bool occupied = false; std::string name;
            if (!resource_flag(b, occupied)) return false;
            if (occupied) b.get_str(name);
            if (b.failed || name.size() > 32768 || name.find('\0') != std::string::npos) {
                b.failed = true; return false;
            }
            d.entries.emplace_back(occupied, std::move(name));
        }
    }
    return !b.failed;
}
bool zc255_directory_skip(SaveStateBlob& b) {
    DirectorySnapshot d; return decode_directory(b, d);
}
bool zc255_directory_read(SaveStateBlob& b, std::set<int32_t>& restored) {
    DirectorySnapshot d;
    if (!decode_directory(b, d) || !restored.insert(d.index).second) { b.failed = true; return false; }
    auto cleanup = [](FLIST* list) { if (list) { list->clear(); free(list); } };
    std::unique_ptr<FLIST, decltype(cleanup)> list(nullptr, cleanup);
    if (d.allocated) {
        list.reset(static_cast<FLIST*>(calloc(1, sizeof(FLIST))));
        if (!list) { b.failed = true; return false; }
        memcpy(list->dir, d.list_path.c_str(), d.list_path.size() + 1);
        list->size = int32_t(d.entries.size());
        for (int32_t i = 0; i < list->size; ++i) {
            if (!d.entries[i].first) continue;
            list->name[i] = _strdup(d.entries[i].second.c_str());
            if (!list->name[i]) { b.failed = true; return false; }
        }
    }
    auto& live = script_dirs[d.index];
    live.clear(); live.reserved = true; live.filepath = d.path;
    live.owned_type = ScriptType(d.owner_type); live.owned_i = d.owner;
    live.list = list.release(); return true;
}

std::map<FILE*, std::string> zc255_script_file_modes;
// Process safety fence, intentionally absent from snapshots: memory rewind cannot
// undo a script's filesystem writes, including a truncating open already closed.
bool zc255_external_script_write_seen = false;
void zc255_script_file_opened(FILE* file, const char* mode) {
    if (!file) return;
    zc255_script_file_modes[file] = mode;
    if (strchr(mode,'w') || strchr(mode,'a') || strchr(mode,'+'))
        zc255_external_script_write_seen = true;
}
bool readonly_file_hash(const std::string& path, uint64_t& hash) {
    auto close = [](FILE* file) { if (file) fclose(file); };
    std::unique_ptr<FILE, decltype(close)> file(fopen(path.c_str(), "rb"), close);
    if (!file) return false;
    hash = 14695981039346656037ULL;
    uint8_t bytes[4096]; size_t read = 0;
    while ((read = fread(bytes, 1, sizeof(bytes), file.get())) != 0)
        for (size_t i = 0; i < read; ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
    return !ferror(file.get());
}
struct FileSnapshot {
    int32_t index = 0, owner_type = 0, owner = 0;
    std::string path, mode;
    bool opened = false, eof = false;
    int64_t position = 0;
    uint64_t contents_hash = 0;
};
bool decode_file(SaveStateBlob& b, FileSnapshot& file) {
    b.get(file.index); b.get_str(file.path); b.get(file.owner_type); b.get(file.owner);
    if (!resource_flag(b, file.opened) || file.index < 0 || file.index >= MAX_USER_FILES) {
        b.failed = true; return false;
    }
    if (file.opened) {
        b.get_str(file.mode); b.get(file.position); resource_flag(b, file.eof); b.get(file.contents_hash);
        if (file.mode != "r" && file.mode != "rb" && file.mode != "rt") b.failed = true;
        if (file.position < 0 || file.path.find('\0') != std::string::npos) b.failed = true;
        uint64_t hash = 0;
        if (b.failed || !readonly_file_hash(file.path, hash) || hash != file.contents_hash) {
            b.failed = true; return false;
        }
    }
    return !b.failed;
}
void zc255_file_write(SaveStateBlob& b, int32_t index) {
    const auto& file = script_files[index];
    b.put(index); b.put_str(file.filepath); b.put(int32_t(file.owned_type)); b.put(file.owned_i);
    b.put(uint8_t(file.file != nullptr));
    if (file.file) {
        auto mode = zc255_script_file_modes.find(file.file);
        // Unknown/writable modes cannot be restored without a virtual filesystem.
        if (mode == zc255_script_file_modes.end() || (mode->second != "r" && mode->second != "rb" && mode->second != "rt")
            || ferror(file.file)) { b.failed = true; return; }
        int64_t position = _ftelli64(file.file); uint64_t hash = 0;
        if (position < 0 || !readonly_file_hash(file.filepath, hash)) { b.failed = true; return; }
        b.put_str(mode->second); b.put(position); b.put(uint8_t(feof(file.file) != 0)); b.put(hash);
    }
}
bool zc255_file_skip(SaveStateBlob& b) { FileSnapshot f; return decode_file(b, f); }
bool zc255_file_read(SaveStateBlob& b, std::set<int32_t>& restored) {
    FileSnapshot f;
    if (!decode_file(b, f) || !restored.insert(f.index).second) { b.failed = true; return false; }
    auto close = [](FILE* file) { if (file) fclose(file); };
    std::unique_ptr<FILE, decltype(close)> opened(nullptr, close);
    if (f.opened) {
        opened.reset(fopen(f.path.c_str(), f.mode.c_str()));
        if (!opened || _fseeki64(opened.get(), f.position, SEEK_SET) != 0) { b.failed = true; return false; }
        if (f.eof && (fgetc(opened.get()) != EOF || !feof(opened.get()))) { b.failed = true; return false; }
    }
    auto& live = script_files[f.index]; live.clear();
    live.reserved = true; live.filepath = f.path; live.owned_type = ScriptType(f.owner_type); live.owned_i = f.owner;
    live.file = opened.release();
    if (live.file) zc255_script_file_modes[live.file] = f.mode;
    return true;
}
}
bool zc255_external_io_blocked() { return zc255_external_script_write_seen; }
