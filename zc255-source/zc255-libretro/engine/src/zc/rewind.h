#ifndef ZC_REWIND_H_
#define ZC_REWIND_H_

// Emulator-style rewind for the player.
//
// Rewinding itself is state restoration (see zc/savestate.h). This module is the visible
// part: while the Rewind control is held, the frames that were just played are shown again
// in reverse - the "tape rewind" an emulator shows - and when the control is released the
// session is restored to the frame that is on screen at that moment.
//
// The frames are kept in a ring buffer of presented frame images, which is cheap (the game
// already rendered them) and independent of how the game state is restored.

#include <filesystem>

// Capture the frame that was just presented. Called once per presented frame while playing;
// a no-op when rewind is unavailable (headless, or no rewind binding).
void rewind_frame_capture();

// Called once per iteration of the game loop, before the frame is simulated. Returns true
// when a rewind preview frame was presented, in which case the caller must skip simulating
// this frame: while the player holds Rewind, the session is frozen and history is shown.
bool rewind_poll();

// True while the player is holding Rewind and history is being shown.
bool rewind_is_previewing();

// The game frame currently on screen during a preview (-1 when not previewing).
int rewind_preview_frame();

// Forget the stored history (quest change, headless run, etc).
void rewind_reset();

// Override the Rewind binding for this run (-rewind-bind / -rewind-button), so it can be
// set without opening the Control Scheme dialog (and so tests can drive it). A key of 0 and
// a button of -1 mean "use the active control scheme".
void rewind_set_binding_override(int key, int button);

// True when a Rewind control is bound (in the active scheme, or by the run override above).
// Binding one is what turns the feature on for a session, including recording it so that a
// snapshot restore can re-cover the gap from the recorded inputs.
bool rewind_control_bound();

// Instrumentation (-rewind-preview-at): once the run reaches `frame`, hold the Rewind
// control for `hold` frames and then release it. Drives the same preview and restore path
// the control does, so the visible rewind can be checked without a display.
void rewind_set_preview_at_frame(int frame, int hold);

#endif
