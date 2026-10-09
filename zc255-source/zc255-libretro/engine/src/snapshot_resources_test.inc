#include <fstream>
#include <filesystem>
bool zc255_snapshot_export(std::vector<uint8_t>&);
bool zc255_snapshot_import(const uint8_t*, size_t);
bool zc255_resources_selftest() {
    const char* report = std::getenv("ZC255_RESOURCES_TEST");
    if (!report || !*report) return false;
    std::vector<uint8_t> original, captured;
    if (!zc255_snapshot_export(original)) {
        std::ofstream out(report); out << "{\"passed\":false,\"stage\":\"original capture\"}\n";
        return false;
    }
    int indices[5] = {}; int selected = 0;
    for (int i = MIN_USER_BITMAPS; i < MAX_USER_BITMAPS && selected < 5; ++i)
        if (!scb.script_created_bitmaps[i].reserved()) indices[selected++] = i;
    int directory = -1, file_index = -1;
    for (int i = 0; i < MAX_USER_DIRS; ++i) if (!script_dirs[i].reserved) { directory = i; break; }
    for (int i = 0; i < MAX_USER_FILES; ++i) if (!script_files[i].reserved) { file_index = i; break; }
    if (selected != 5 || directory < 0 || file_index < 0) {
        std::ofstream out(report); out << "{\"passed\":false,\"stage\":\"free resource slots\"}\n";
        return false;
    }
    const int depths[5] = {8,15,16,24,32}; int colors[5] = {};
    bool created = true;
    for (int n = 0; n < 5; ++n) {
        auto& bmp = scb.script_created_bitmaps[indices[n]];
        bmp.width = 4; bmp.height = 3; bmp.depth = depths[n]; bmp.reserve();
        bmp.u_bmp = create_bitmap_ex(depths[n], 4, 3);
        if (!bmp.u_bmp) { created = false; break; }
        colors[n] = depths[n] == 8 ? 23 : makecol_depth(depths[n], 200, 70, 130);
        clear_to_color(bmp.u_bmp, colors[n]);
    }
    auto& dir = script_dirs[directory];
    dir.reserved = true; dir.filepath = "historic-directory/";
    dir.list = static_cast<FLIST*>(calloc(1, sizeof(FLIST)));
    if (!dir.list) created = false;
    else {
        strcpy(dir.list->dir, "historic-directory/"); dir.list->size = 1;
        dir.list->name[0] = _strdup("past-entry.txt");
        if (!dir.list->name[0]) created = false;
    }
    std::string data_path = std::string(report) + ".input";
    { std::ofstream data(data_path, std::ios::binary); data << "abcdef"; }
    auto& file = script_files[file_index]; file.reserved = true; file.filepath = data_path;
    file.file = fopen(data_path.c_str(), "rb");
    if (!file.file) created = false;
    else { zc255_script_file_modes[file.file] = "rb"; _fseeki64(file.file, 2, SEEK_SET); }
    bool saved = created && zc255_snapshot_export(captured);
    bool restored = false, pixels = false, listing = false, offset = false, writable_refused = false;
    if (saved) {
        for (int n = 0; n < 5; ++n) clear_to_color(scb.script_created_bitmaps[indices[n]].u_bmp, 0);
        free(dir.list->name[0]); dir.list->name[0] = _strdup("future-entry.txt");
        _fseeki64(file.file, 5, SEEK_SET);
        restored = zc255_snapshot_import(captured.data(), captured.size());
        if (restored) {
            pixels = true;
            for (int n = 0; n < 5; ++n) {
                auto& bmp = scb.script_created_bitmaps[indices[n]];
                pixels = pixels && bmp.u_bmp && bitmap_color_depth(bmp.u_bmp) == depths[n]
                    && getpixel(bmp.u_bmp, 2, 1) == colors[n];
            }
            listing = script_dirs[directory].list && script_dirs[directory].list->size == 1
                && std::string(script_dirs[directory].list->name[0]) == "past-entry.txt";
            auto& livefile = script_files[file_index];
            offset = livefile.file && _ftelli64(livefile.file) == 2 && fgetc(livefile.file) == 'c';
            // Only metadata is switched to a writable mode; no disk writes occur.
            zc255_script_file_modes[livefile.file] = "rb+";
            std::vector<uint8_t> invalid;
            writable_refused = !zc255_snapshot_export(invalid);
            zc255_script_file_modes[livefile.file] = "rb";
        }
    }
    bool rollback = zc255_snapshot_import(original.data(), original.size());
    bool closed_write_refused = false;
    if (rollback) {
        // A successful writable open poisons subsequent capture even after close.
        FILE* writable = fopen(data_path.c_str(), "rb+");
        if (writable) {
            zc255_script_file_opened(writable,"rb+"); fclose(writable);
            std::vector<uint8_t> rejected;
            closed_write_refused = !zc255_snapshot_export(rejected)
                && !zc255_snapshot_import(original.data(),original.size());
        }
    }
    bool passed = saved && restored && pixels && listing && offset && writable_refused && rollback && closed_write_refused;
    std::ofstream out(report);
    out << "{\"passed\":" << (passed ? "true" : "false")
        << ",\"all_bitmap_depths\":" << (pixels ? "true" : "false")
        << ",\"historical_directory\":" << (listing ? "true" : "false")
        << ",\"readonly_file_position\":" << (offset ? "true" : "false")
        << ",\"writable_capture_refused\":" << (writable_refused ? "true" : "false")
        << ",\"closed_write_capture_and_import_refused\":" << (closed_write_refused ? "true" : "false")
        << ",\"original_restored\":" << (rollback ? "true" : "false") << "}\n";
    return passed;
}
