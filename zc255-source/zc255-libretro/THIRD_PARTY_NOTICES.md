# Licensing, third-party notices and modifications

The Zelda Classic engine and this gameplay-core derivative are distributed under the GNU General Public License version 3. The full licence is in `LICENSE`. Each dependency and asset retains its own notices and licence; the table below is an index, not a replacement for those terms.

Original authors' copyright notices are preserved. No claim is made to authorship of the upstream engine, dependencies, samples or quest content.

| Component | Licence / notice | Corresponding source and exact notices |
|---|---|---|
| Zelda Classic / ZQuest Classic 2.55 | GPL-3.0; original notices retained | `engine/`, `LICENSE` |
| Allegro 5 | zlib licence | `dependencies/allegro5-src/LICENSE.txt` |
| Allegro legacy compatibility layer | Allegro giftware terms | `engine/third_party/allegro_legacy/LICENSE`, `AUTHORS` |
| SDL 2 | zlib; included subcomponents retain their notices | `dependencies/sdl2-src/LICENSE.txt`, `src/hidapi/LICENSE*`, `src/video/yuv2rgb/LICENSE` |
| SDL GameControllerDB | zlib licence | `engine/third_party/SDL_GameControllerDB/LICENSE` |
| AsmJit | zlib licence | `dependencies/asmjit-src/LICENSE.md` |
| Native File Dialog Extended | zlib licence | `dependencies/nativefiledialog-src/LICENSE` |
| Game_Music_Emu | LGPL-2.1-or-later; embedded emu2413 MIT; Nuked YM2612 selected | `dependencies/gme_external-src/license.txt`, `gme/ext/LICENSE`, notices in `gme/` |
| DUMB tracker decoder | DUMB licence, exact custom terms preserved | `engine/third_party/dumb/LICENSE` |
| TinySoundFont / SFZero | MIT | Complete notice at the beginning of `engine/src/tsf.h` |
| minimp3 | CC0 public-domain dedication | Beginning of `engine/include/minimp3/minimp3.h` |
| stb_vorbis | MIT option of its dual licence | `licenses/stb-vorbis.txt`, `engine/src/vendor/stb_vorbis.c` |
| zlib | zlib licence | `engine/third_party/zlib/LICENSE` |
| libpng | libpng licence | `engine/third_party/libpng/LICENSE` |
| libjpeg-turbo | IJG, BSD-3-Clause and zlib notices | `engine/third_party/libjpeg-turbo/LICENSE.md`, `README.ijg` and inline notices |
| al_loadpng | Public-domain notice | `engine/third_party/al_loadpng/LICENSE` |
| fmt | MIT | `engine/third_party/fmt/LICENSE` |
| nlohmann/json | MIT | `engine/third_party/json/LICENSE`, `dependencies/json-src/LICENSE.MIT` |
| nonstd expected-lite | Boost Software License 1.0 | `engine/third_party/nonstd/LICENSE` and inline notice |
| stduuid | MIT | `dependencies/stduuid-src/LICENSE` |
| GeneralUser GS 2.0.3 SoundFont | GeneralUser GS licence v2.0 | `licenses/GeneralUser-GS.txt`, `assets/payload/default.sf2` |
| Upstream music agreement | Separate upstream music agreement, with required credit to Isabelle Chiming | `licenses/upstream-music-agreement.pdf` |

Copies of available component licence, copyright and authorship files are also collected under `licenses/`. Header-only notices remain complete in their source headers. Any additional per-file notices take precedence for their files.

## GeneralUser GS credit and source

GeneralUser GS by **S. Christian Collins**: https://www.schristiancollins.com/generaluser.php

Source and full terms: https://github.com/mrbumpy409/GeneralUser-GS

The SoundFont is included unmodified, renamed to the engine's expected `default.sf2`. Its author's full licence, including its sample-provenance explanation, is retained. The previous private Roland GS bank is excluded from this public package: its redistribution grant was not established from the bank's documentation. This is an asset change rather than an engine or quest-format change.

## Upstream assets and quests

Stock compatibility assets and their notices come from the upstream engine distribution. The supplied music agreement requires credit to **Isabelle Chiming** whenever covered music is distributed; that credit and the full agreement are supplied here. No downloaded playable quest library, quest screenshots, user music packs, quest progress or save states are included.

The software licence does not grant rights to independent quests or their art/music, nor imply endorsement by Nintendo or the Zelda Classic, RetroArch or RetroBat teams. Keep the licences and credits supplied by quest authors with their separately distributed content.

## Derivative modifications

This is an altered gameplay-only derivative. Changes include the in-process libretro host, RetroArch input and audio integration, complete native state capture/restore and rewind caches, script/map/tile tracking, collision restoration, core options and selectable cheats, quest-progress management, enhanced-music integration and embedded-asset preparation. Legacy audio snapshot additions and modified dependencies are supplied in the corresponding source tree.

The public packaging removes private absolute includes and build paths, disables updater/crash-report/Git-hook setup, provides an offline build recipe and uses a redistribution-licensed MIDI bank. It does not claim to be an official upstream release.

## Binary and source distribution

Distribute the matching complete source archive beside each DLL and RetroBat release. It includes the static LGPL components and the recipe needed to rebuild and relink them; recipients may replace those components and produce their own DLL. Include this notice, `LICENSE` and the complete `licenses/` directory with binary distributions. Additional Microsoft system/runtime DLLs are not redistributed by this package.
