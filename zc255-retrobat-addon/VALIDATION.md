# Death-menu rewind validation — 2026-10-07

The death Save/Continue menu now runs incrementally at the normal engine frame boundary. Menu selection, button-release handling and confirmation flash are serialized. Restoring gameplay restores the living engine/script ownership and leaves the menu. Original death/exit/save hook order is preserved; save commits happen only after confirming Save. Fatal transitions and native death-animation captures retain the preceding complete snapshot rather than recording an immediately fatal frame or an unsupported C++ continuation.

- Actual workspace RetroArch internal rewind passed at intervals one and four: the death menu returned to living gameplay, no progress file was written, no state rejection or timeout occurred, and the frontend exited normally. The test used a temporary local command interface addressed to 127.0.0.1; it closed with the process. No network setting was added to the delivered package or installed frontend.
- Classic first quest (2.50) with every-frame capture and the 2.55 example quest with four-frame capture: 35 exact gameplay/video/PCM continuation frames after returning before death, 20 exact menu continuation frames, menu/gameplay restores in both directions, and corrupt-menu rejection without live-state changes passed.
- Continue, Save, Quit, unloading with Save highlighted, no-save, no-continue and instant-continue rules passed. Only confirmed Save wrote progress. Menu and confirmation-flash states loaded in fresh independent processes and resumed normally.
- Relevant prior fixes were checked: Rite of the Storm's white-star Save, Cancel, Quit and progress reload passed with 35 exact restored continuation frames; Banana Blood God's name/dialogue/restart reached playable gameplay with four-frame captures.
- Real RetroArch testing caught a zero-health pre-animation snapshot that re-entered death on the next simulated rewind frame. Retaining the preceding complete state fixed it; both frontend intervals were re-tested successfully.

DLL SHA-256: `7b69b06cf91a2fab5ee7ec71c0c8d49ddc0b0cf03d6ca161b7698e83feae65c9`

Native state format is now 19 and UI state version 4. Create fresh RetroArch states with this DLL; ordinary quest progress remains compatible. Tests cover the stated cases, not every quest. This environment had no usable physical audio endpoint during the frontend test; matching core PCM was checked, physical sound output and steady gameplay speed were not newly measured.

## Previous scripted save fix validation

The previous build fixed the reproduced Rite of the Storm save-menu hang and Banana Blood God scripted save/restart failure. Other nested native save menus retain a C++ continuation; state capture and restore are refused promptly while they are active, instead of waiting for an unreachable gameplay checkpoint. The new incremental death menu above is separately serializable.

- Rite of the Storm: first white-star platform Save, Don't Save, Quit confirmation/reload, progress reload after frontend unload, repeated capture rejection in the menu, restore rejection without advancing its frame, then 35 exact restored gameplay/video/PCM continuation frames passed. Captures every four frames after the menu remained valid.
- Banana Blood God: introductory name entry, Finnigan dialogue/restart, and gameplay passed with captures every four frontend frames. Saved progress reloaded in a fresh process, retaining Finnigan, with 35 exact restored gameplay/video/PCM frames.
- Hosted single-quest saves skip the desktop multi-slot order file on both write and load; this removes the reproduced throwing path canonicalization failure. Quest Quit retains selected content, while explicit frontend unload still unloads it.
- Existing ordinary progress is compatible. Native states are build-specific; create fresh states after replacing the DLL. The preceding public release and private DLLs are preserved.

Previous DLL SHA-256: `d30043773a8b5e572e79f30afc12bc2bb555d7113750364c15333e53e884ec89`

The checks below describe the preceding public release, not a repeated full regression batch for this fix.
# Public release validation

The Windows x64 DLL was built successfully from this staged source tree with the supplied offline recipe. A generated font header's old upstream path comment was subsequently removed; this changes no compiled font data or logic.

- Titan's Gate and Trinity Worlds: 240 gameplay/video frames each exactly matched the preceding private release. Each public build test restored 35 exact continuation frames including video, audio and gameplay data.
- Fresh independent producer/consumer processes: SPC and OGG enhanced music restored 35 exact continuation frames each. Native music and legacy audio snapshot checks passed. Missing and changed OGG files rejected restoration without changing live state; restoring the original asset succeeded.
- Workspace RetroArch: single DLL and separate quest, all thirteen core options registered, WASAPI audio initialized, 600 gameplay reads, 150 rewind captures at interval four, approximately 59.93 FPS, zero missing or dropped core audio frames. No network command listener was enabled. Held-rewind speed was not measured in this release check.
- Publication scan: readable ASCII and UTF-16 content in the staged source, DLLs and embedded resources checked for the private Windows account name, personal profile paths, common API-token formats and private-key headers. No matches remained. History, personal configuration, saves, logs and compiler/debug outputs are excluded from the archives; ZIP timestamps are fixed. Pattern scanning cannot prove the absence of every possible secret.

The public MIDI bank is GeneralUser GS 2.0.3, replacing the privately used bank whose redistribution grant was not established. This changes MIDI timbre. Gameplay, controls, rewind and enhanced-music implementations are retained. Ordinary quest progress stays compatible; native save states are tied to the build and must be created anew.

These checks cover the stated quests/scenes and audio cases, rather than every quest or every possible gameplay path. The eleven enhanced-music formats listed in the README were tested during the preceding engine work; the public packaging changes no decoder code.

DLL SHA-256: `b5a9419d13f845745575f31de8cea10cf771976b7700ebc4c3a0bc81248513a8`
