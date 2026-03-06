#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
//  Touch Gesture System
// ═══════════════════════════════════════════════════════════════════
//
//  Layered on top of the raw CST328 touch driver.  Define zones
//  (rectangles or circles), specify which gestures each zone should
//  detect, and register a callback.  Call Touch_Gesture_Process()
//  from your loop — it reads the hardware, runs the state machine,
//  and fires callbacks when gestures complete.
//
//  Usage:
//    TouchZone btnPlay;
//    btnPlay.id     = 1;
//    btnPlay.shape  = ZONE_RECT;
//    btnPlay.x      = 80;  btnPlay.y = 200;
//    btnPlay.w      = 80;  btnPlay.h = 40;
//    btnPlay.detect = GESTURE_CLICK | GESTURE_DOUBLE_CLICK;
//    btnPlay.callback = onPlayButton;
//    Touch_Gesture_AddZone(&btnPlay);
//
//    void onPlayButton(TouchEvent& evt) {
//      if (evt.gesture == GESTURE_CLICK) { ... }
//    }
// ═══════════════════════════════════════════════════════════════════

// ── Gesture type flags (bitmask — combine with |) ───────────────────
#define GESTURE_NONE          0x00
#define GESTURE_CLICK         0x01    // Single tap
#define GESTURE_DOUBLE_CLICK  0x02    // Two taps within threshold
#define GESTURE_SWIPE_UP      0x04
#define GESTURE_SWIPE_DOWN    0x08
#define GESTURE_SWIPE_LEFT    0x10
#define GESTURE_SWIPE_RIGHT   0x20
#define GESTURE_DRAG          0x40    // Continuous position while held
#define GESTURE_LONG_PRESS    0x80    // Held without moving

// Convenience combos
#define GESTURE_SWIPE_ANY     (GESTURE_SWIPE_UP | GESTURE_SWIPE_DOWN | GESTURE_SWIPE_LEFT | GESTURE_SWIPE_RIGHT)
#define GESTURE_ALL           0xFF

// ── Drag phase ──────────────────────────────────────────────────────
#define DRAG_START   0
#define DRAG_MOVE    1
#define DRAG_END     2

// ── Zone shape ──────────────────────────────────────────────────────
#define ZONE_RECT    0
#define ZONE_CIRCLE  1

// ── Tuning constants ────────────────────────────────────────────────
#define GESTURE_CLICK_MAX_MS      300    // Max duration for a tap
#define GESTURE_CLICK_MAX_DIST    15     // Max movement (pixels) for a tap
#define GESTURE_DBLCLICK_GAP_MS   350    // Max gap between two taps
#define GESTURE_LONG_PRESS_MS     600    // Hold time for long press
#define GESTURE_SWIPE_MIN_DIST    30     // Min travel for a swipe
#define GESTURE_DRAG_START_DIST   8      // Movement before drag begins

// ── Max simultaneous zones ──────────────────────────────────────────
#define TOUCH_MAX_ZONES  32

// ── Forward declaration ─────────────────────────────────────────────
struct TouchEvent;

// ── Callback type ───────────────────────────────────────────────────
typedef void (*TouchGestureCallback)(TouchEvent& evt);

// ── Touch event (passed to callbacks) ───────────────────────────────
struct TouchEvent {
  uint8_t  zoneID;        // Which zone triggered this
  uint8_t  gesture;       // Which gesture (single flag, not bitmask)
  int16_t  x, y;          // Position where gesture occurred / current pos
  int16_t  startX, startY;// Where the touch began
  int16_t  deltaX, deltaY;// Offset from start (useful for drag/swipe)
  uint8_t  dragPhase;     // DRAG_START / DRAG_MOVE / DRAG_END
  uint32_t duration;      // How long the touch lasted (ms)
};

// ── Zone definition ─────────────────────────────────────────────────
struct TouchZone {
  uint8_t  id;            // User-assigned ID (0-255)
  bool     enabled;       // Set false to temporarily disable
  uint8_t  shape;         // ZONE_RECT or ZONE_CIRCLE
  int16_t  x, y;          // Top-left for rect, center for circle
  int16_t  w, h;          // Width/height for rect (unused for circle)
  int16_t  r;             // Radius for circle (unused for rect)
  uint8_t  detect;        // Bitmask of GESTURE_* flags to detect
  TouchGestureCallback callback;  // Called when a gesture is recognized
  void*    userData;       // Optional user data pointer
};

// ═══════════════════════════════════════════════════════════════════
//  PUBLIC API
// ═══════════════════════════════════════════════════════════════════

// Add a zone.  Returns true on success.
// The TouchZone struct is copied — caller can discard after adding.
bool Touch_Gesture_AddZone(const TouchZone* zone);

// Remove a zone by ID.  Returns true if found and removed.
bool Touch_Gesture_RemoveZone(uint8_t id);

// Remove all zones.
void Touch_Gesture_ClearZones(void);

// Enable or disable a zone by ID.
void Touch_Gesture_EnableZone(uint8_t id, bool enabled);

// Update a zone's position/size.  Useful for scrollable layouts.
bool Touch_Gesture_MoveZone(uint8_t id, int16_t x, int16_t y);
bool Touch_Gesture_ResizeZone(uint8_t id, int16_t w, int16_t h);

// Change which gestures a zone detects.
bool Touch_Gesture_SetDetect(uint8_t id, uint8_t detectMask);

// Get a pointer to a zone by ID (or nullptr).
TouchZone* Touch_Gesture_GetZone(uint8_t id);

// Get number of active zones.
uint8_t Touch_Gesture_ZoneCount(void);

// ── Process (call from loop) ────────────────────────────────────────
// Reads the touch hardware, runs the gesture state machine, and
// fires callbacks.  Call this once per loop iteration.
// Returns true if any gesture was detected this frame.
bool Touch_Gesture_Process(void);

// ── State queries ───────────────────────────────────────────────────
// Is a touch currently active (finger down)?
bool Touch_IsPressed(void);

// Get the current raw touch position (regardless of zones).
// Returns false if not currently touched.
bool Touch_GetRawXY(int16_t* x, int16_t* y);

// ── Helper: create zones easily ─────────────────────────────────────

// Create a rectangular zone.  Returns a filled TouchZone struct.
inline TouchZone Touch_MakeRect(uint8_t id, int16_t x, int16_t y,
                                int16_t w, int16_t h,
                                uint8_t detect, TouchGestureCallback cb,
                                void* userData = nullptr)
{
  TouchZone z = {};
  z.id       = id;
  z.enabled  = true;
  z.shape    = ZONE_RECT;
  z.x = x; z.y = y; z.w = w; z.h = h; z.r = 0;
  z.detect   = detect;
  z.callback = cb;
  z.userData = userData;
  return z;
}

// Create a circular zone.
inline TouchZone Touch_MakeCircle(uint8_t id, int16_t cx, int16_t cy,
                                  int16_t radius,
                                  uint8_t detect, TouchGestureCallback cb,
                                  void* userData = nullptr)
{
  TouchZone z = {};
  z.id       = id;
  z.enabled  = true;
  z.shape    = ZONE_CIRCLE;
  z.x = cx; z.y = cy; z.w = 0; z.h = 0; z.r = radius;
  z.detect   = detect;
  z.callback = cb;
  z.userData = userData;
  return z;
}
