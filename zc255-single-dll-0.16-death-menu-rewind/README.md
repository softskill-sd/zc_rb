# Death-menu rewind update

Replace your core DLL with `zc255_libretro.dll` and start a fresh RetroArch process. At the death Save/Continue menu, hold your usual RetroArch rewind hotkey to return to living gameplay. Rewind must be enabled and have recorded earlier gameplay.

The menu can also be captured and loaded as a RetroArch state. Death-animation captures retain the preceding complete state, so rewind jumps back before death. Continue, Save and Quit retain their normal behavior; rewind itself does not save quest progress.

Create new RetroArch states after changing the DLL. Older native states are build-specific; ordinary quest progress remains compatible. One-frame and four-frame internal RetroArch rewind were tested, along with exact gameplay/video/audio continuation and fresh-process menu-state loading.

SHA-256: `7b69b06cf91a2fab5ee7ec71c0c8d49ddc0b0cf03d6ca161b7698e83feae65c9`

For redistribution, use the matching source and licence-bearing core or RetroBat archives in `outputs/github-ready/`. This loose DLL is provided for updating an existing installation. No installed RetroBat files were changed.
