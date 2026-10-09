# Zelda Classic 2.55 for RetroBat

This is a manual-copy add-on for 64-bit Windows RetroBat. Nothing has been installed or changed in your RetroBat installation.

1. Close RetroBat. Open `copy-to-retrobat` and copy its **contents** into your RetroBat folder (the folder containing `retrobat.exe`). Merge the folders.
2. Put your separate `.qst` files into `RetroBat/roms/zeldaclassic/`.
3. Start RetroBat again. Open **Zelda Classic 2.55** and select a quest. If necessary, refresh the game list or restart EmulationStation.

The add-on supplies these new files:

- `emulators/retroarch/cores/zc255_libretro.dll` — the complete gameplay core.
- `emulators/retroarch/info/zc255_libretro.info` — optional RetroArch menu metadata.
- `emulationstation/.emulationstation/es_systems_zeldaclassic.cfg` — the custom RetroBat system entry.

The core itself needs only its single DLL and your quest. The metadata and system entry integrate it into RetroBat. No engine asset folders or helper executable need to be copied. The core creates its embedded asset cache and progress saves under the save directory RetroArch supplies.

The launch command uses RetroBat's normal libretro launcher, including its controller configuration. Do not rename the DLL: the system entry selects core `zc255`, which resolves to `zc255_libretro.dll`. This package adds a separate system file; it does not replace the main `es_systems.cfg`.

## System display correction

The updated system entry uses its own `zeldaclassic` theme identifier. The first package reused `ports`, which could show the Ports artwork instead of Zelda Classic in the system carousel even while alphabetical navigation listed Zelda Classic correctly. To update an existing installation manually, replace only `emulationstation/.emulationstation/es_systems_zeldaclassic.cfg` with the updated file and fully restart RetroBat. No DLL update is needed. Without Zelda Classic artwork in your selected theme, EmulationStation can use its default theme and text label; the exact appearance depends on that theme. If the entry is still hidden, check Main Menu → Game Collection Settings → Systems Displayed for Zelda Classic. Ensure at least one `.qst` is in `roms/zeldaclassic`.

## Controls and features

Use RetroArch's Quick Menu for Core Options and save states. **SELECT invokes the quest's map action; START opens the inventory/subscreen.** A quest can disable its full-screen map or supply its own map script. R2 is the single refill button and fills only resources selected in Core Options. L3/R3 are unused. Core Options allow button swapping, optional analog movement and disabling the refill shortcut. Cheats never need a password.

Quest progress saves follow the game's own save points and save/death menus. The added manual save shortcut, 30-second forced save and save-on-unload have been removed. Closing RetroArch does not create an extra quest-progress save. RetroArch save states remain separate and available.

The add-on includes `emulationstation/.emulationstation/es_features_zeldaclassic.cfg`. Copy it along with the other new add-on files and fully restart RetroBat. From Zelda Classic's **Advanced System Options → Gameplay → Rewind**, choose **Off (Faster Gameplay)** or **On**. You can also set this for an individual quest through its Advanced Game Options. Save states work with rewind off. While running, RetroArch also exposes Settings → Frame Throttle → Rewind; choose a value in RetroBat for future launches, since it regenerates settings.

This build tracks engine changes to tiles, combos and quest maps. Unchanged groups reuse their complete saved encoding. Scripts that could change maps conservatively invalidate the map tracking. Every state remains complete and can load in a fresh process. Audit tests compare tracking with full scans and deliberately bypass hooks to verify the safe fallback. The new map and combo tests passed 10 and 17 cases respectively; malformed states were rejected without changing gameplay.

This build retains the faster validated-map restore path and further reduces repeated capture work. Dirty-combo temporary encoding buffers are reused transactionally, with retained scratch capped at 2 MiB. Small groups of fields are written together using at most 512 bytes of stack memory; the state bytes are unchanged. In paired Hero of Dreams Palm Island capture-every-frame tests, warm static captures improved from **12.89 to 11.90 ms (about 8%)**, and moving captures from **13.38 to 11.94 ms (about 11%)**. Nearby restores remained similar, around **102 ms** in this test. These results exclude cold captures and vary by quest and area. Inventory, collision bindings into quest maps, full tile/combo/map data, malformed-state rejection, scripted quests, map UI and fresh-process map restoration pass. RetroArch captured all 360 frames at interval 1 and 91 states at interval 4. The current Windows session had no default audio endpoint, so this phase verifies matching core PCM and zero missing/dropped frames but does not claim speaker-output or audio-paced FPS validation. Earlier build measurements of 60.0 FPS at interval 4 and 54.4 FPS at interval 1 remain historical results.

Start with rewind **On** and an interval of **4 frames**. In RetroBat, open **Advanced System Options → Gameplay → Rewind Capture Interval → Every Frame** for more precise rewind; it can still slow gameplay. You can also set the interval per quest. If another area or quest slows down, try **8 or 16 frames**, or turn rewind off. Larger intervals make backward movement less precise. Save states still work with rewind off. Use your configured frontend rewind hotkey; L2 has no core action. Shaders use the normal RetroArch shader menu.

In RetroBat, **Advanced System Options → Gameplay → Cheats Default** selects the **On** or **Off** options profile; you can also select it per quest. The profiles are supplied under `emulators/retroarch/config/ZeldaClassic255`. Changes made in RetroArch persist in the selected profile. Without an explicit RetroBat profile, the core allows cheats by default.

Inside the game, open **RetroArch Quick Menu → Core Options**. **Allow cheats (master switch)** controls all cheat actions. Toggle invulnerability and walk-through-walls separately. The native walls cheat also grants invulnerability; disabling walls removes that effect unless the separate invulnerability switch remains enabled.

Select **R2 refill: hearts / magic / rupees / bombs / arrows** independently, then press **R2** to refill just those resources once. For example, enabling rupees and bombs refills both; enabling hearts as well adds hearts to the same button. Nothing refills continuously. All resource selections initially start off. Bombs uses the game's existing bomb cheat, including super bombs. Turn the master switch off to suppress all cheat actions without forgetting your selections.

The optional legacy cheat file under `cheats/retroarch/Zelda Classic 2.55/` remains available through Quick Menu → Cheats → Load Cheat File. Its refill action uses the same resource selections. Use Core Options for the complete set of individual menu switches.

## Clear saved quest progress

While a quest is running, open **RetroArch Quick Menu → Core Options → Clear this quest's saved progress**, choose **request clear**, then resume the game. A visible confirmation screen appears: **A clears and restarts the quest; B cancels**. Held buttons are ignored when the screen opens. This uses the standard RetroPad A/B buttons.

Only this quest's active progress save is cleared. The old file is retained in the save directory as `zc255-<quest identity>.sav.cleared-<timestamp>`. Other quests' saves and RetroArch save-state files are left intact. Loading a save state can restore its older in-memory progress; this action clears the ordinary on-disk quest save. The option returns to keep automatically on current RetroArch versions. On older frontends, choose keep before requesting clear again. A previously stored request never clears anything at quest startup.

Close RetroArch before updating the DLL. Save states from earlier prototype DLLs are incompatible with this build; ordinary quest progress saves remain compatible.

## Scope and verification

The Trinity Worlds “Drawing map…” crash is fixed. Its full-screen map now supports rewind and save states, including map position, zoom, display setting and image. Opening/closing, restoring the drawing stage, every-frame and every-four-frame captures, and loading a map state in a fresh process were checked. Picture, sound and gameplay matched over 114 frames after restoration; movement resumes after closing the map. Close RetroArch before replacing the DLL. Older prototype save states will not load in this build; normal quest progress saves remain compatible.

Gameplay-only Zelda Classic 2.55, core version 0.14. MIDI, legacy sound effects and enhanced external music work. A fresh RetroArch process per quest is the supported workflow. Save states from earlier prototype builds are incompatible; ordinary quest progress saves remain compatible.

The add-on's XML, core naming and archive contents are checked before packaging. The custom launch route was reviewed against current official RetroBat sources. It has **not** been launched inside your RetroBat installation, which was left untouched; version-specific integration remains unverified there.

Official references:

- [RetroBat custom system FAQ](https://retrobat.forumgaming.fr/t135-faq-retrobat-wip#4073)
- [RetroBat system templates](https://github.com/RetroBat-Official/retrobat/blob/master/system/templates/emulationstation/es_systems.cfg)
- [RetroBat libretro launcher](https://github.com/RetroBat-Official/emulatorlauncher/blob/master/emulatorLauncher/Generators/LibRetro.Generator.cs)

Corresponding engine source and build recipe are retained in this workspace under `work/zc255-core/phase-lifecycle/engine-source-allegro-static-lifecycle-test-embedded` and `work/zc255-core/phase-host/build_standalone.py`. This is a local prototype package; license text is included alongside this README.
# Controller input correction

This package now includes the input ownership fix. Close RetroArch completely before manually replacing `emulators/retroarch/cores/zc255_libretro.dll`. The core no longer initializes a second physical joystick backend inside RetroArch. Frontend input registrations, movement, restored controls/video/audio and real RetroArch sound were checked. Physical 8BitDo buttons and F1 in your installation still need confirmation. Older build savestates are incompatible; ordinary progress saves remain usable.

## Enhanced music

Enhanced music plays through RetroArch audio inside the single core DLL. Playback and fresh-process state continuation were checked for OGG, MP3, WAV, MOD, XM, IT, S3M, SPC, NSF, GBS and VGM. External MID and standalone BRR are not connected; MIDI embedded in a quest continues to work. Separate .z script sources are not needed for gameplay.

Keep the music filenames expected by the quest. For Adventure.qst, place the pack in the adjacent Adventure.qst_music folder. Preserve relative subpaths explicitly referenced by the quest. The core also searches the quest directory and adjacent music folder.

Track selection, volume, pause and music position are restored with states. SPC/NSF/GBS/VGM use a shared decoded-audio cache capped at 64 MiB, keeping nearby rewind from replaying the emulated song. Fresh-process or older out-of-cache restoration reconstructs audio from the source file and can take longer; exceptionally long reconstruction can safely reject after its 25-second preparation budget. Missing or changed music files reject restoration without changing live gameplay. Music files must remain available beside the quest.

OGG/MP3/tracker/WAV decode once when loaded, with a 256 MiB aggregate decoded-data limit. Initial track loading may briefly pause while decoding. WAV supports mono/stereo PCM 8/16/24/32-bit and 32-bit float at 8–192 kHz. Standard uncompressed VGM is tested; compressed VGZ is not claimed. Close RetroArch before replacing the DLL. Older prototype save states are incompatible; ordinary quest progress saves remain compatible.

## Scripted quest rewind performance

Scripts now mark the quest-map groups they access instead of invalidating every quest map whenever any script runs. Complete map fields, notes and all FFC slots remain in each state. Globally invalidated maps also use the existing parallel comparison path.

At the four-frame setting, repeated moving captures in Titan's Gate fell from approximately 71 to 11 ms, and Titan's Gate 2 from 88 to 13 ms. Both opening sequences ran at approximately 59.9 FPS in workspace RetroArch tests with sound and rewind recording enabled. This is not a guarantee for every area or quest. Held rewind still has separate restoration costs; nearby restores measured approximately 77–94 ms before the next restore optimization.

Map audits and exact restored picture/sound/gameplay checks passed for both Titan quests, Hero of Dreams, Trinity Worlds and the scripted example quest. Actual script setters, retained map references and inactive FFC changes were also checked. Close RetroArch before updating; old prototype save states require a fresh state, while ordinary quest progress remains compatible.

## Public release

The DLL uses GeneralUser GS 2.0.3 for embedded MIDI. MIDI timbre differs from the previous private prototype. Gameplay and enhanced-music code are retained. Native save states are build-specific; create fresh states after replacing the DLL. Ordinary quest progress remains compatible. See THIRD_PARTY_NOTICES.md and licenses/. The matching zc255-source.zip must accompany redistribution.
