#include "Touch_Gestures.h"
#include "Touch_CST328.h"
#include "Display_ST7789.h"   // for lcd_rotation, LCD_PANEL_W, LCD_PANEL_H

// ═══════════════════════════════════════════════════════════════════
//  Internal state
// ═══════════════════════════════════════════════════════════════════

// State machine phases
enum GestureState : uint8_t {
  GS_IDLE = 0,        // No active touch
  GS_TOUCH_DOWN,      // Finger down, classifying gesture
  GS_DRAGGING,        // Movement exceeded threshold, sending drag events
  GS_WAIT_DBLCLICK,   // First tap done, waiting for possible second tap
};

// ── Zone storage ────────────────────────────────────────────────────
static TouchZone  zones_[TOUCH_MAX_ZONES];
static uint8_t    zoneCount_ = 0;

// ── State machine ───────────────────────────────────────────────────
static GestureState state_       = GS_IDLE;
static bool     prevTouching_    = false;     // Was the screen touched last frame?
static bool     curTouching_     = false;     // Is it touched this frame?
static int16_t  curX_  = 0, curY_  = 0;      // Current touch position
static int16_t  downX_ = 0, downY_ = 0;      // Where finger first touched
static uint32_t downTime_ = 0;               // millis() at touch-down
static int8_t   downZoneIdx_ = -1;           // Index into zones_ where touch began
static bool     longPressFired_ = false;      // Already sent long press for this touch?

// ── Double-click tracking ───────────────────────────────────────────
static uint32_t lastTapTime_     = 0;         // When the previous tap ended
static int8_t   lastTapZoneIdx_  = -1;        // Which zone was tapped last
static int16_t  lastTapX_ = 0, lastTapY_ = 0;// Where the last tap landed

// ═══════════════════════════════════════════════════════════════════
//  Internal helpers
// ═══════════════════════════════════════════════════════════════════

static inline int32_t sq_i32(int32_t v) { return v * v; }

// Test if point (px, py) is inside a zone
static bool pointInZone(const TouchZone& z, int16_t px, int16_t py)
{
  if (!z.enabled) return false;

  if (z.shape == ZONE_RECT) {
    return (px >= z.x && px < z.x + z.w &&
            py >= z.y && py < z.y + z.h);
  }
  if (z.shape == ZONE_CIRCLE) {
    int32_t dist2 = sq_i32((int32_t)px - z.x) + sq_i32((int32_t)py - z.y);
    return dist2 <= sq_i32((int32_t)z.r);
  }
  return false;
}

// Find the topmost zone that contains (px, py).
// Returns index into zones_[], or -1 if none.
// Searches in reverse order so later-added zones are "on top".
static int8_t findZoneAt(int16_t px, int16_t py)
{
  for (int8_t i = (int8_t)(zoneCount_ - 1); i >= 0; i--) {
    if (pointInZone(zones_[i], px, py)) return i;
  }
  return -1;
}

// Find zone index by ID, or -1
static int8_t findZoneByID(uint8_t id)
{
  for (uint8_t i = 0; i < zoneCount_; i++) {
    if (zones_[i].id == id) return (int8_t)i;
  }
  return -1;
}

// Distance between two points
static inline int16_t dist(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
  int32_t dx = x2 - x1;
  int32_t dy = y2 - y1;
  // Fast integer approximation — good enough for gesture thresholds
  int32_t ax = abs(dx);
  int32_t ay = abs(dy);
  return (int16_t)((ax > ay) ? (ax + ay / 2) : (ay + ax / 2));
}

// Fire a callback on a zone (with bounds checking)
static void fireEvent(int8_t zoneIdx, TouchEvent& evt)
{
  if (zoneIdx < 0 || zoneIdx >= zoneCount_) return;
  TouchZone& z = zones_[zoneIdx];
  if (!z.enabled || !z.callback) return;
  // Only fire if the zone is interested in this gesture type
  if (!(z.detect & evt.gesture)) return;
  evt.zoneID = z.id;
  z.callback(evt);
}

// Build a basic event from current state
static TouchEvent makeEvent(uint8_t gesture, int8_t zoneIdx)
{
  TouchEvent evt = {};
  if (zoneIdx >= 0 && zoneIdx < zoneCount_) {
    evt.zoneID = zones_[zoneIdx].id;
  }
  evt.gesture  = gesture;
  evt.x        = curX_;
  evt.y        = curY_;
  evt.startX   = downX_;
  evt.startY   = downY_;
  evt.deltaX   = curX_ - downX_;
  evt.deltaY   = curY_ - downY_;
  evt.duration  = millis() - downTime_;
  evt.dragPhase = 0;
  return evt;
}

// Determine swipe direction from delta
static uint8_t swipeDirection(int16_t dx, int16_t dy)
{
  int16_t ax = abs(dx);
  int16_t ay = abs(dy);

  if (ax > ay) {
    return (dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
  } else {
    return (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
  }
}

// ═══════════════════════════════════════════════════════════════════
//  Read hardware (adapts CST328 driver to our needs)
// ═══════════════════════════════════════════════════════════════════
//
// We read the touch controller directly every frame rather than
// relying solely on the ISR, because the CST328 may not generate
// continuous interrupts while the finger is held.  We access the
// touch_data struct directly to avoid Touch_Get_XY() which clears
// the data after each call.

extern struct CST328_Touch touch_data;   // from Touch_CST328.cpp

static void readHardware(void)
{
  extern uint8_t Touch_interrupts;

  prevTouching_ = curTouching_;

  // Always poll the touch controller to get current state.
  // Touch_Read_Data reads register 0xD005 which reports current
  // active points, so it works for both new touches and held state.
  //
  // IMPORTANT: Touch_Read_Data() does NOT clear touch_data.points
  // when there's no touch — it just returns.  So we clear it first;
  // if there ARE touches, Touch_Read_Data will overwrite it.
  touch_data.points = 0;
  Touch_Read_Data();

  // Consume the ISR flag if it was set (already read above)
  Touch_interrupts = false;

  // Read directly from the shared struct (protected by noInterrupts
  // inside Touch_Read_Data).
  if (touch_data.points > 0) {
    curTouching_ = true;

    // Raw coordinates are always in the panel's native 240×320 portrait space.
    // Remap them to match the current display rotation.
    int16_t rx = (int16_t)touch_data.coords[0].x;
    int16_t ry = (int16_t)touch_data.coords[0].y;

    switch (lcd_rotation) {
      default:
      case 0:  // portrait — no transform
        curX_ = rx;
        curY_ = ry;
        break;
      case 1:  // landscape (MADCTL 0x60: MX|MV)
        curX_ = ry;
        curY_ = (LCD_PANEL_W - 1) - rx;
        break;
      case 2:  // portrait inverted (MADCTL 0xC0: MY|MX)
        curX_ = (LCD_PANEL_W - 1) - rx;
        curY_ = (LCD_PANEL_H - 1) - ry;
        break;
      case 3:  // landscape inverted (MADCTL 0xA0: MY|MV)
        curX_ = (LCD_PANEL_H - 1) - ry;
        curY_ = rx;
        break;
    }
  } else {
    curTouching_ = false;
  }
}

// ═══════════════════════════════════════════════════════════════════
//  PUBLIC: Zone management
// ═══════════════════════════════════════════════════════════════════

bool Touch_Gesture_AddZone(const TouchZone* zone)
{
  if (!zone || zoneCount_ >= TOUCH_MAX_ZONES) return false;

  // Prevent duplicate IDs — overwrite if same ID exists
  int8_t existing = findZoneByID(zone->id);
  if (existing >= 0) {
    zones_[existing] = *zone;
    return true;
  }

  zones_[zoneCount_] = *zone;
  zoneCount_++;
  return true;
}

bool Touch_Gesture_RemoveZone(uint8_t id)
{
  int8_t idx = findZoneByID(id);
  if (idx < 0) return false;

  // Shift remaining zones down
  for (uint8_t i = (uint8_t)idx; i < zoneCount_ - 1; i++) {
    zones_[i] = zones_[i + 1];
  }
  zoneCount_--;

  // Fix up state references
  if (downZoneIdx_ == idx)       downZoneIdx_ = -1;
  else if (downZoneIdx_ > idx)   downZoneIdx_--;
  if (lastTapZoneIdx_ == idx)    lastTapZoneIdx_ = -1;
  else if (lastTapZoneIdx_ > idx) lastTapZoneIdx_--;

  return true;
}

void Touch_Gesture_ClearZones(void)
{
  zoneCount_ = 0;
  state_ = GS_IDLE;
  downZoneIdx_ = -1;
  lastTapZoneIdx_ = -1;
}

void Touch_Gesture_EnableZone(uint8_t id, bool enabled)
{
  int8_t idx = findZoneByID(id);
  if (idx >= 0) zones_[idx].enabled = enabled;
}

bool Touch_Gesture_MoveZone(uint8_t id, int16_t x, int16_t y)
{
  int8_t idx = findZoneByID(id);
  if (idx < 0) return false;
  zones_[idx].x = x;
  zones_[idx].y = y;
  return true;
}

bool Touch_Gesture_ResizeZone(uint8_t id, int16_t w, int16_t h)
{
  int8_t idx = findZoneByID(id);
  if (idx < 0) return false;
  zones_[idx].w = w;
  zones_[idx].h = h;
  return true;
}

bool Touch_Gesture_SetDetect(uint8_t id, uint8_t detectMask)
{
  int8_t idx = findZoneByID(id);
  if (idx < 0) return false;
  zones_[idx].detect = detectMask;
  return true;
}

TouchZone* Touch_Gesture_GetZone(uint8_t id)
{
  int8_t idx = findZoneByID(id);
  if (idx < 0) return nullptr;
  return &zones_[idx];
}

uint8_t Touch_Gesture_ZoneCount(void)
{
  return zoneCount_;
}

// ═══════════════════════════════════════════════════════════════════
//  PUBLIC: State queries
// ═══════════════════════════════════════════════════════════════════

bool Touch_IsPressed(void)
{
  return curTouching_;
}

bool Touch_GetRawXY(int16_t* x, int16_t* y)
{
  if (!curTouching_) return false;
  if (x) *x = curX_;
  if (y) *y = curY_;
  return true;
}

// ═══════════════════════════════════════════════════════════════════
//  STATE MACHINE
// ═══════════════════════════════════════════════════════════════════
//
//  GS_IDLE
//    ├── finger down in zone → GS_TOUCH_DOWN
//    └── (nothing)
//
//  GS_TOUCH_DOWN  (finger is held)
//    ├── movement > drag threshold → fire DRAG_START → GS_DRAGGING
//    ├── duration > long press time → fire LONG_PRESS (stay in state)
//    └── finger up:
//        ├── movement > swipe min → fire SWIPE_* → GS_IDLE
//        ├── duration < click max → it's a tap:
//        │   ├── zone wants double-click → GS_WAIT_DBLCLICK
//        │   └── else → fire CLICK → GS_IDLE
//        └── else → GS_IDLE (held too long for click, not enough for swipe)
//
//  GS_DRAGGING  (finger held + moved past threshold)
//    ├── finger still down → fire DRAG_MOVE each frame
//    └── finger up:
//        ├── total movement > swipe min → fire DRAG_END + SWIPE_*
//        └── else → fire DRAG_END → GS_IDLE
//
//  GS_WAIT_DBLCLICK  (finger lifted after first tap)
//    ├── finger down in same zone within gap → fire DOUBLE_CLICK → GS_IDLE
//    ├── finger down in different zone → fire CLICK for 1st, process new
//    └── timeout → fire CLICK for 1st → GS_IDLE
//
// ═══════════════════════════════════════════════════════════════════

bool Touch_Gesture_Process(void)
{
  readHardware();

  bool gestureDetected = false;
  uint32_t now = millis();

  switch (state_) {

    // ────────────────────────────────────────────────────────────
    case GS_IDLE:
    {
      // Check if double-click window expired (from previous tap)
      if (lastTapZoneIdx_ >= 0 &&
          (now - lastTapTime_) > GESTURE_DBLCLICK_GAP_MS) {
        // Fire the delayed single click
        TouchEvent evt = {};
        evt.zoneID   = zones_[lastTapZoneIdx_].id;
        evt.gesture  = GESTURE_CLICK;
        evt.x        = lastTapX_;
        evt.y        = lastTapY_;
        evt.startX   = lastTapX_;
        evt.startY   = lastTapY_;
        fireEvent(lastTapZoneIdx_, evt);
        lastTapZoneIdx_ = -1;
        gestureDetected = true;
      }

      // New touch?
      if (curTouching_ && !prevTouching_) {
        downX_ = curX_;
        downY_ = curY_;
        downTime_ = now;
        longPressFired_ = false;
        downZoneIdx_ = findZoneAt(curX_, curY_);

        if (downZoneIdx_ >= 0) {
          state_ = GS_TOUCH_DOWN;
        }
        // If touch not in any zone, ignore it (stay IDLE)
      }
      break;
    }

    // ────────────────────────────────────────────────────────────
    case GS_TOUCH_DOWN:
    {
      if (!curTouching_) {
        // ── Finger lifted ──
        uint32_t dur = now - downTime_;
        int16_t  d   = dist(downX_, downY_, curX_, curY_);
        // Use the position before release for end coords
        // (curX_/curY_ are still the last known position)
        int16_t dx = curX_ - downX_;
        int16_t dy = curY_ - downY_;
        int16_t totalDist = dist(downX_, downY_, curX_, curY_);

        if (totalDist >= GESTURE_SWIPE_MIN_DIST && downZoneIdx_ >= 0) {
          // ── Swipe ──
          uint8_t dir = swipeDirection(dx, dy);
          if (zones_[downZoneIdx_].detect & dir) {
            TouchEvent evt = makeEvent(dir, downZoneIdx_);
            evt.duration = dur;
            fireEvent(downZoneIdx_, evt);
            gestureDetected = true;
          }
          lastTapZoneIdx_ = -1;  // not a tap
          state_ = GS_IDLE;
        }
        else if (dur <= GESTURE_CLICK_MAX_MS && d <= GESTURE_CLICK_MAX_DIST) {
          // ── Tap ──
          if (downZoneIdx_ >= 0 &&
              (zones_[downZoneIdx_].detect & GESTURE_DOUBLE_CLICK)) {
            // Check if this is the second tap of a double-click
            if (lastTapZoneIdx_ == downZoneIdx_ &&
                (now - lastTapTime_) <= GESTURE_DBLCLICK_GAP_MS) {
              // ── Double click! ──
              TouchEvent evt = makeEvent(GESTURE_DOUBLE_CLICK, downZoneIdx_);
              evt.duration = dur;
              fireEvent(downZoneIdx_, evt);
              gestureDetected = true;
              lastTapZoneIdx_ = -1;
              state_ = GS_IDLE;
            } else {
              // First tap — wait for possible second
              // If there was a pending tap on a different zone, fire it now
              if (lastTapZoneIdx_ >= 0 && lastTapZoneIdx_ != downZoneIdx_) {
                TouchEvent old_evt = {};
                old_evt.zoneID  = zones_[lastTapZoneIdx_].id;
                old_evt.gesture = GESTURE_CLICK;
                old_evt.x       = lastTapX_;
                old_evt.y       = lastTapY_;
                old_evt.startX  = lastTapX_;
                old_evt.startY  = lastTapY_;
                fireEvent(lastTapZoneIdx_, old_evt);
                gestureDetected = true;
              }
              lastTapZoneIdx_ = downZoneIdx_;
              lastTapTime_    = now;
              lastTapX_       = curX_;
              lastTapY_       = curY_;
              state_ = GS_IDLE;  // Go idle, double-click check happens in IDLE
            }
          } else {
            // Zone doesn't want double-click — fire single click immediately
            TouchEvent evt = makeEvent(GESTURE_CLICK, downZoneIdx_);
            evt.duration = dur;
            fireEvent(downZoneIdx_, evt);
            gestureDetected = true;
            lastTapZoneIdx_ = -1;
            state_ = GS_IDLE;
          }
        }
        else {
          // Lifted but didn't qualify as click or swipe
          lastTapZoneIdx_ = -1;
          state_ = GS_IDLE;
        }
      }
      else {
        // ── Finger still down ──
        int16_t  d   = dist(downX_, downY_, curX_, curY_);
        uint32_t dur = now - downTime_;

        // Check for drag start
        if (d >= GESTURE_DRAG_START_DIST &&
            downZoneIdx_ >= 0 &&
            (zones_[downZoneIdx_].detect & GESTURE_DRAG)) {
          TouchEvent evt = makeEvent(GESTURE_DRAG, downZoneIdx_);
          evt.dragPhase = DRAG_START;
          fireEvent(downZoneIdx_, evt);
          gestureDetected = true;
          lastTapZoneIdx_ = -1;  // can't be a double-click anymore
          state_ = GS_DRAGGING;
        }
        // Check for long press
        else if (!longPressFired_ && dur >= GESTURE_LONG_PRESS_MS &&
                 d <= GESTURE_CLICK_MAX_DIST &&
                 downZoneIdx_ >= 0 &&
                 (zones_[downZoneIdx_].detect & GESTURE_LONG_PRESS)) {
          TouchEvent evt = makeEvent(GESTURE_LONG_PRESS, downZoneIdx_);
          evt.duration = dur;
          fireEvent(downZoneIdx_, evt);
          gestureDetected = true;
          longPressFired_ = true;
          lastTapZoneIdx_ = -1;
        }
      }
      break;
    }

    // ────────────────────────────────────────────────────────────
    case GS_DRAGGING:
    {
      if (!curTouching_) {
        // ── Finger lifted — end drag ──
        TouchEvent evt = makeEvent(GESTURE_DRAG, downZoneIdx_);
        evt.dragPhase = DRAG_END;
        evt.duration = now - downTime_;
        fireEvent(downZoneIdx_, evt);
        gestureDetected = true;

        // Also check if it qualifies as a swipe
        int16_t totalDist = dist(downX_, downY_, curX_, curY_);
        if (totalDist >= GESTURE_SWIPE_MIN_DIST && downZoneIdx_ >= 0) {
          uint8_t dir = swipeDirection(curX_ - downX_, curY_ - downY_);
          if (zones_[downZoneIdx_].detect & dir) {
            TouchEvent swipeEvt = makeEvent(dir, downZoneIdx_);
            swipeEvt.duration = now - downTime_;
            fireEvent(downZoneIdx_, swipeEvt);
          }
        }

        state_ = GS_IDLE;
      }
      else {
        // ── Still dragging — send move events ──
        TouchEvent evt = makeEvent(GESTURE_DRAG, downZoneIdx_);
        evt.dragPhase = DRAG_MOVE;
        evt.duration = now - downTime_;
        fireEvent(downZoneIdx_, evt);
        gestureDetected = true;
      }
      break;
    }

    default:
      state_ = GS_IDLE;
      break;
  }

  return gestureDetected;
}
