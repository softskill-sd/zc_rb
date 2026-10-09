#include <fstream>
#include <cstdlib>
bool zc255_snapshot_export(std::vector<uint8_t>&);
bool zc255_snapshot_import(const uint8_t*, size_t);

bool zc255_inventory_selftest() {
    const char* report = std::getenv("ZC255_INVENTORY_TEST");
    if (!report || !*report || !game || !Playing) return false;
    std::vector<uint8_t> original, before;
    bool original_saved = zc255_snapshot_export(original);
    int sword = -1;
    for (int i = 0; i < MAXITEMS; ++i)
        if (itemsbuf[i].family == itype_sword && !item_disabled(i)
            && (sword < 0 || itemsbuf[i].fam_type < itemsbuf[sword].fam_type))
            sword = i;
    bool captured = false, granted = false, restored = false, cache_restored = false;
    bool rollback = false;
    int future_lookup = -1, past_lookup = -2;
    if (original_saved && sword >= 0) {
        // Exercise ownership lookup independently of any quest's scripted override.
        game->OverrideItems[itype_sword] = -2;
        for (int i = 0; i < MAXITEMS; ++i)
            if (itemsbuf[i].family == itype_sword) game->set_item(i, false);
        // This populates the past cache with "no sword" through the real engine lookup.
        int before_lookup = current_item_id(itype_sword, false, false, false);
        captured = before_lookup == -1 && zc255_snapshot_export(before);
        if (captured) {
            game->set_item(sword, true);
            future_lookup = current_item_id(itype_sword, false, false, false);
            granted = game->get_item(sword) && future_lookup >= 0;
            restored = zc255_snapshot_import(before.data(), before.size());
            if (restored) {
                past_lookup = current_item_id(itype_sword, false, false, false);
                cache_restored = !game->get_item(sword) && past_lookup == -1;
            }
        }
        rollback = zc255_snapshot_import(original.data(), original.size());
    }
    bool passed = captured && granted && restored && cache_restored && rollback;
    std::ofstream out(report);
    out << "{\"passed\":" << (passed ? "true" : "false")
        << ",\"sword_id\":" << sword << ",\"future_lookup\":" << future_lookup
        << ",\"restored_lookup\":" << past_lookup
        << ",\"ownership_and_lookup_restored\":" << (cache_restored ? "true" : "false")
        << ",\"original_restored\":" << (rollback ? "true" : "false") << "}\n";
    return passed;
}
