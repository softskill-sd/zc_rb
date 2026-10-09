# Zelda Classic 2.55 libretro gameplay core

A Windows x64 libretro core for playing Zelda Classic quests in RetroArch and RetroBat. The released core is a single DLL: load a separate `.qst` file and keep any quest music beside it. No launcher process or quest editor is included in the release.

This is an unofficial, experimental port of Zelda Classic 2.55. It is independent of Nintendo, the Zelda Classic team, RetroArch and RetroBat.

## Features

- RetroArch save states and native rewind, with complete gameplay, scripts, inventory, map definitions and collision state.
- Rewind from the death Save/Continue menu back into living gameplay, including serializable menu selection and confirmation animation.
- Quest progress saves, plus a confirmed option to clear only the active quest's progress, retaining a backup.
- RetroPad controls, Select for the quest map, configurable button layouts and analog-to-D-pad input.
- Core options for cheat availability, invulnerability, walking through walls and selected refills. One refill action replenishes only the resources enabled in the menu.
- Rewind settings in the RetroBat add-on: disabled or capture every 1, 4, 8 or 16 frames.
- Embedded MIDI and enhanced music through RetroArch's audio output.
- Standard RetroArch shaders can process the core's video output.

Enhanced music formats tested: **OGG, MP3, WAV, MOD, XM, IT, S3M, SPC, NSF, GBS and uncompressed VGM**. External MID, standalone BRR and compressed VGZ are not claimed as supported. Quest scripts must already be compiled into the quest; separate `.z` source files are not needed for gameplay.

## Install in RetroArch

1. Download the Windows x64 core release and extract `zc255_libretro.dll` into RetroArch's `cores` directory.
2. Copy `zc255_libretro.info` into RetroArch's `info` directory.
3. Load the core, then load a `.qst` file. Quest downloads and quest music packs are supplied separately by their authors.

For `Adventure.qst`, keep its enhanced music in the adjacent `Adventure.qst_music` directory. Preserve filenames and any subdirectories referenced by the quest. The quest directory itself is also searched.

Map: Select. Other bindings and cheat/refill choices are described by RetroArch's Core Options menu. Ordinary quest progress follows the quest's natural save flow; the core adds no forced gameplay-save shortcut.

## Install in RetroBat

Use the separate **RetroBat add-on release ZIP**, which includes the core, core information, custom system/features XML, cheats and preset files. Follow the README inside that archive. It is a manual add-on and does not overwrite the main `es_systems.cfg`. Custom-system integration can differ across RetroBat versions and themes.

## Rewind and performance

Four-frame capture is the recommended starting setting. Every-frame recording is available but costs more CPU and rewind memory. Scripts mark the quest-map groups they access, avoiding whole-quest scans when a script only changes unrelated gameplay data. Full map fields, notes and inactive FFC slots are preserved.

At the death Save/Continue menu, hold your usual RetroArch rewind hotkey to return to gameplay. Fatal transitions and the native death animation retain the preceding complete snapshot, so rewind jumps back before death. The death menu itself supports state capture and restore. Other nested native save dialogs still reject state operations promptly. Rewind does not write quest progress; selecting Save retains the quest's normal save flow.

Focused tests of Titan's Gate showed repeated captures dropping from approximately 71 to 11 ms, and Titan's Gate 2 from 88 to 13 ms. Opening-sequence RetroArch tests ran close to 60 FPS with four-frame recording. These measurements concern specific scenes, not every quest or later area. Restoring states has separate costs and held rewind may remain slower.

SPC/NSF/GBS/VGM playback uses a bounded 64 MiB decoded-audio cache. Warm rewind within that window avoids regenerating the song. Cold or older out-of-cache restores reconstruct from the music file and may take longer; exceptionally long reconstruction can reject safely. Other supported music formats decode once under a 256 MiB aggregate limit.

## Compatibility and limitations

Windows x64 is the supported release platform. Use a fresh RetroArch process when changing quests; full same-process reinitialization is not promised. Quest compatibility is broad but not universal. Save states are tied to the native build; states from older prototype DLLs may not load. Ordinary quest progress saves remain compatible with the preceding private prototype.

Music files must remain available when restoring states. Missing or modified assets reject restoration without changing gameplay. Moving a quest/music installation can invalidate old states containing the original asset location.

The public build uses **GeneralUser GS** for embedded MIDI synthesis. The earlier private build used a different SoundFont whose redistribution grant was not established. MIDI timbre differs; gameplay, controls, rewind and enhanced-music code are retained.

## Build from source

Install Visual Studio 2022 C++ Build Tools with a Windows 10/11 SDK, CMake, Ninja and Python 3. Run `./build.ps1` in PowerShell, or `python build.py`. The builder discovers the installed x64 compiler and SDK. The output is `dist/zc255_libretro.dll`. Flex/Bison are not required to play quests with their embedded scripts.

Dependencies and embedded assets are supplied in this source tree; the build disables dependency downloads, crash reporting, the updater and Git hooks. `./build.ps1 -ConfigureOnly` checks configuration. Build products, saves, logs and private environment files are ignored by Git.

The source includes upstream shared engine code for building the gameplay target. Build only the `zplayer` target used by the supplied recipe. The release ZIP contains no editor executable or additional decoder DLL.

## Licensing and credits

The core/engine is distributed under **GNU GPL version 3**, with the full text in [LICENSE](LICENSE). Dependencies retain their individual licences. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/` for exact notices, including LGPL components, header-only decoders, controller mappings and music assets.

Publish the matching source archive alongside binary releases so recipients can rebuild and relink the LGPL components. Preserve all original notices. Quest authors retain the rights to their quests, art and music; this release includes no downloaded quest library or third-party quest packs.

Original engine: the Zelda Classic / ZQuest Classic contributors and Armageddon Games. MIDI synthesizer: Bernhard Schelling's TinySoundFont. GeneralUser GS: S. Christian Collins. Upstream music agreement credit: Isabelle Chiming. Complete dependency credits remain in their source and licence notices.

This source package contains no private repository history, personal build logs, save files or account configuration. Release binaries are rebuilt with mapped source paths and a basename-only debug filename.
